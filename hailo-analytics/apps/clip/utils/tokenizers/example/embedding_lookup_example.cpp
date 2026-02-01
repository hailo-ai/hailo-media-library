#include <fstream>
#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <string>
#include <cstdint>
#include <cassert>
#include <cmath>

#include "hailo/hailort.hpp"
#include "../common_utils.hpp"

using namespace hailort;

#define USE_INPUT_FLOAT32_AUTO_QUANTIZE (1)
#define USE_OUTPUT_FLOAT32_AUTO_DEQUANTIZE (1)

#define CLIP_TEXT_ENCODER_HEF                                                                                          \
    "/data/clip_vit_b_32_text_encoder_yuval_2025_07_08.hef" // "/data/clip_vit_b_32_text_encoder_mz.hef"  //
                                                            // "/data/clip_resnet_50x4_text_encoder_h15h.hef"
#define CLIP_TEXT_ENCODER_DIM (512)                         //(512)   // (640)

#define EMBEDDING_LOOKUP_PATH                                                                                          \
    "/data/clip_vit_b32_embedding.bin" // "/data/clip_vit_b32_embedding.bin"  // "/data/clip_resnet50x4_embedding.bin"

struct EmbeddingTable
{
    uint32_t rows;
    uint32_t cols;
    std::vector<float> data; // row-major [rows * cols]

    bool load(const std::string &path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return false;

        f.read(reinterpret_cast<char *>(&rows), sizeof(uint32_t));
        f.read(reinterpret_cast<char *>(&cols), sizeof(uint32_t));

        data.resize(static_cast<size_t>(rows) * cols);
        f.read(reinterpret_cast<char *>(data.data()), data.size() * sizeof(float));
        return true;
    }

    // Lookup one token ID -> pointer to embedding vector
    const float *operator[](int token_id) const
    {
        if (token_id < 0 || static_cast<uint32_t>(token_id) >= rows)
            return nullptr;
        return &data[token_id * cols];
    }
};

// Generic struct to load the matrix/vector from your binary format
struct Matrix
{
    uint32_t rows;
    uint32_t cols;
    std::vector<float> data; // Stored in row-major order

    bool load(const std::string &path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
        {
            std::cerr << "Error: Cannot open file " << path << std::endl;
            return false;
        }

        f.read(reinterpret_cast<char *>(&rows), sizeof(uint32_t));
        f.read(reinterpret_cast<char *>(&cols), sizeof(uint32_t));

        data.resize(static_cast<size_t>(rows) * cols);
        f.read(reinterpret_cast<char *>(data.data()), data.size() * sizeof(float));

        if (!f)
        {
            std::cerr << "Error: Failed to read data from " << path << std::endl;
            return false;
        }

        std::cout << "Loaded matrix from " << path << " (" << rows << " x " << cols << ")" << std::endl;
        return true;
    }

    // Access element at (r, c)
    float at(uint32_t r, uint32_t c) const
    {
        return data[r * cols + c];
    }
};

void save_as_npy(const std::vector<float> &data, int batch_size, int seq_len, int dim, const std::string &filename)
{
    std::ofstream file(filename, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("Failed to open file: " + filename);
    }

    // Magic string and version
    file.write("\x93NUMPY", 6);
    file.put(0x01); // major version
    file.put(0x00); // minor version

    // Construct header dictionary
    std::ostringstream header_stream;
    header_stream << "{'descr': '<f4', 'fortran_order': False, 'shape': (" << batch_size << ", " << seq_len << ", "
                  << dim << "), }";

    std::string header = header_stream.str();

    // Pad the header to make total header length divisible by 16
    size_t header_len = header.size() + 1;          // +1 for newline
    size_t padding = 16 - ((10 + header_len) % 16); // 10 bytes for magic + version + header_len
    header.append(padding, ' ');
    header += '\n';

    // Write header length
    uint16_t header_size = static_cast<uint16_t>(header.size());
    file.write(reinterpret_cast<const char *>(&header_size), sizeof(header_size));

    // Write header
    file.write(header.c_str(), header.size());

    // Write data
    file.write(reinterpret_cast<const char *>(data.data()), data.size() * sizeof(float));
}

std::vector<float> build_hailo_input_tensor(const std::vector<int> &tokens, const EmbeddingTable &table,
                                            int max_len = 77)
{
    std::vector<int> token_ids;
    token_ids.reserve(max_len);

    // Add start token
    token_ids.push_back(49406);

    // Add sentence tokens
    for (int t : tokens)
    {
        if ((int)token_ids.size() >= max_len - 1)
            break; // leave space for end token
        token_ids.push_back(t);
    }

    // Add end token
    token_ids.push_back(49407);

    // Pad with 0 or EOT (49407)? if shorter than max_len
    while ((int)token_ids.size() < max_len)
    {
        token_ids.push_back(0);
    }

    // Build tensor [1 x max_len x dim]
    std::vector<float> tensor;
    tensor.resize(max_len * table.cols);

    for (int i = 0; i < max_len; i++)
    {
        const float *emb = table[token_ids[i]];
        if (!emb)
            continue;
        std::memcpy(&tensor[i * table.cols], emb, table.cols * sizeof(float));
    }

    return tensor; // row-major [77 x dim]
}

// Function to perform the text projection: result = weights * input + bias
std::vector<float> apply_text_projection(const std::vector<float> &last_hidden_state, const Matrix &weights,
                                         const Matrix &bias)
{
    // Sanity checks
    assert(last_hidden_state.size() == weights.rows && "Input vector size must match weight matrix input dimension");
    assert(weights.rows == weights.cols && "Weight matrix should be square");
    assert(bias.rows == 1 && "Bias should be a row vector");
    assert(bias.cols == weights.cols && "Bias and weights output dimensions must match");

    const uint32_t output_dim = weights.cols;
    std::vector<float> projected_embedding(output_dim);

    // 1. Matrix-Vector Multiplication: projected_embedding = weights * last_hidden_state
    for (uint32_t i = 0; i < output_dim; ++i)
    {
        float sum = 0.0f;
        for (uint32_t j = 0; j < weights.rows; ++j)
        {
            // Dot product of the i-th row of the weights and the input vector
            sum += weights.at(i, j) * last_hidden_state[j];
        }
        projected_embedding[i] = sum;
    }

    // 2. Add Bias: projected_embedding = projected_embedding + bias
    for (uint32_t i = 0; i < output_dim; ++i)
    {
        projected_embedding[i] += bias.data[i];
    }

    return projected_embedding;
}

// L2 normalization function
void l2_normalize(std::vector<float> &v)
{
    float norm = 0.0f;
    for (float x : v)
    {
        norm += x * x;
    }
    norm = std::sqrt(norm);

    if (norm > 1e-6)
    {
        for (float &x : v)
        {
            x /= norm;
        }
    }
}

void run_inference(const std::vector<float> &input_tensor)
{
    std::cout << "Running inference with input tensor of size: " << input_tensor.size() << " floats\n";

    auto vdevice = VDevice::create().expect("Failed create vdevice");
    std::cout << "VDevice created" << std::endl;

    // Create infer model from HEF file.
    auto infer_model = vdevice->create_infer_model(CLIP_TEXT_ENCODER_HEF).expect("Failed to create infer model");
    std::cout << "InferModel created" << std::endl;

    // Get input quantization info
    std::vector<hailo_quant_info_t> input_quant_info = infer_model->input()->get_quant_infos();
    std::cout << "Input quantization info - scale: " << input_quant_info[0].qp_scale
              << ", zero point: " << input_quant_info[0].qp_zp << std::endl;

    // Get output quantization info
    std::vector<hailo_quant_info_t> output_quant_info = infer_model->output()->get_quant_infos();
    std::cout << "Output quantization info - scale: " << output_quant_info[0].qp_scale
              << ", zero point: " << output_quant_info[0].qp_zp << std::endl;

#if USE_INPUT_FLOAT32_AUTO_QUANTIZE // Use float32 input
    infer_model->input()->set_format_type(HAILO_FORMAT_TYPE_FLOAT32);
    std::cout << "Set input format_type to float32" << std::endl;
#else // Use uint16 input with manual quantization
    infer_model->input()->set_format_type(HAILO_FORMAT_TYPE_UINT16);
    std::cout << "Set input format_type to uint16" << std::endl;

    // Manually quantize the input tensor to uint16
    std::vector<uint16_t> quantized_input(input_tensor.size());
    for (size_t i = 0; i < input_tensor.size(); i++)
    {
        int32_t q = static_cast<int32_t>(
            std::round(input_tensor[i] / input_quant_info[0].qp_scale + input_quant_info[0].qp_zp));
        if (q < 0)
            q = 0;
        if (q > 65535)
            q = 65535; // Clamp to uint16 range
        quantized_input[i] = static_cast<uint16_t>(q);
    }

    // Print the first 10 quantized values
    std::cout << "Quantized input (first 10 values): ";
    for (int i = 0; i < 10; i++)
    {
        std::cout << quantized_input[i] << " ";
    }
    std::cout << std::endl;

#endif

#if USE_OUTPUT_FLOAT32_AUTO_DEQUANTIZE
    infer_model->output()->set_format_type(HAILO_FORMAT_TYPE_FLOAT32);
    std::cout << "Set output format_type to float32" << std::endl;
#else
    infer_model->output()->set_format_type(HAILO_FORMAT_TYPE_UINT8);
    std::cout << "Set output format_type to uint8" << std::endl;
#endif

    // Configure the infer model
    auto configured_infer_model = infer_model->configure().expect("Failed to create configured infer model");
    std::cout << "ConfiguredInferModel created" << std::endl;

    // Create infer bindings
    auto bindings = configured_infer_model.create_bindings().expect("Failed to create infer bindings");

    std::vector<std::shared_ptr<uint8_t>> input_buffer_guards;
    std::vector<std::shared_ptr<uint8_t>> output_buffer_guards;
    std::vector<DmaMappedBuffer> input_buffer_map_guards;
    std::vector<DmaMappedBuffer> output_buffer_map_guards;

    for (const auto &input_name : infer_model->get_input_names())
    {
        size_t input_frame_size = infer_model->input(input_name)->get_frame_size();
        auto input_buffer_guard = SystemUtils::page_aligned_alloc(input_frame_size);

        std::cout << "Allocated input buffer for " << input_name << " of size " << input_frame_size << " bytes\n";

#if USE_INPUT_FLOAT32_AUTO_QUANTIZE
        // Copy input_tensor data to input_buffer_guard
        if (input_tensor.size() * sizeof(float) != input_frame_size)
        {
            throw std::runtime_error("Input tensor size does not match model input size");
        }
        std::memcpy(input_buffer_guard.get(), input_tensor.data(), input_frame_size);
#else
        // Copy quantized_input data to input_buffer_guard
        if (quantized_input.size() * sizeof(uint16_t) != input_frame_size)
        {
            throw std::runtime_error("Quantized input tensor size does not match model input size");
        }
        std::memcpy(input_buffer_guard.get(), quantized_input.data(), input_frame_size);

#endif
        // Print the first 10 float of input_tensor and input_buffer_guard
        std::cout << "input_tensor (first 10 floats): ";
        for (int i = 0; i < 10; i++)
        {
            std::cout << input_tensor[i] << " ";
        }
        std::cout << std::endl;

        input_buffer_guards.push_back(input_buffer_guard);
        auto input_mapping = DmaMappedBuffer::create(*vdevice, input_buffer_guard.get(), input_frame_size,
                                                     HAILO_DMA_BUFFER_DIRECTION_H2D)
                                 .expect("Failed to map input buffer to VDevice");
        input_buffer_map_guards.push_back(std::move(input_mapping));

        auto status = bindings.input(input_name)->set_buffer(MemoryView(input_buffer_guard.get(), input_frame_size));
        if (HAILO_SUCCESS != status)
        {
            throw hailort_error(status, "Failed to set infer output buffer");
        }
    }

    for (const auto &output_name : infer_model->get_output_names())
    {
        size_t output_frame_size = infer_model->output(output_name)->get_frame_size();
        auto output_buffer_guard = SystemUtils::page_aligned_alloc(output_frame_size);

        std::cout << "Allocated output buffer for " << output_name << " of size " << output_frame_size << " bytes\n";

        output_buffer_guards.push_back(output_buffer_guard);
        auto output_mapping = DmaMappedBuffer::create(*vdevice, output_buffer_guard.get(), output_frame_size,
                                                      HAILO_DMA_BUFFER_DIRECTION_D2H)
                                  .expect("Failed to map output buffer to VDevice");
        output_buffer_map_guards.push_back(std::move(output_mapping));

        auto status =
            bindings.output(output_name)->set_buffer(MemoryView(output_buffer_guard.get(), output_frame_size));
        if (HAILO_SUCCESS != status)
        {
            throw hailort_error(status, "Failed to set infer output buffer");
        }
    }

    auto job =
        configured_infer_model
            .run_async(
                bindings,
                [&output_buffer_guards, &output_quant_info](const AsyncInferCompletionInfo &completion_info) {
                    // check infer status
                    if (completion_info.status != HAILO_SUCCESS)
                    {
                        std::cerr << "Failed to run async infer, Hailort status = " << completion_info.status
                                  << std::endl;
                        return;
                    }

#if USE_OUTPUT_FLOAT32_AUTO_DEQUANTIZE

                    float *float_ptr = reinterpret_cast<float *>(output_buffer_guards[0].get());
                    size_t final_text_embedding_pos = 6 * CLIP_TEXT_ENCODER_DIM; // 6th token embedding start position
                    std::cout << "Inference output (first 10 floats of 6th token): ";
                    for (int i = 0; i < 10; i++)
                    {
                        std::cout << float_ptr[final_text_embedding_pos + i] << " ";
                    }
                    std::cout << std::endl;

                    // Copy final text embedding (6th token) to a vector
                    std::vector<float> text_embedding(CLIP_TEXT_ENCODER_DIM);
                    std::memcpy(text_embedding.data(), &float_ptr[final_text_embedding_pos],
                                CLIP_TEXT_ENCODER_DIM * sizeof(float));
#else

                    uint8_t *uint8_ptr = reinterpret_cast<uint8_t *>(output_buffer_guards[0].get());
                    size_t final_text_embedding_pos = 6 * CLIP_TEXT_ENCODER_DIM; // 6th token embedding start position
                    std::cout << "Inference output (first 10 uint8 of 6th token): ";
                    for (int i = 0; i < 10; i++)
                    {
                        std::cout << static_cast<int>(uint8_ptr[final_text_embedding_pos + i]) << " ";
                    }
                    std::cout << std::endl;

                    // Dequantize the final text embedding (6th token)
                    std::vector<float> text_embedding(CLIP_TEXT_ENCODER_DIM);
                    for (size_t i = 0; i < CLIP_TEXT_ENCODER_DIM; i++)
                    {
                        text_embedding[i] =
                            (static_cast<float>(uint8_ptr[final_text_embedding_pos + i]) - output_quant_info[0].qp_zp) *
                            output_quant_info[0].qp_scale;
                    }

                    std::cout << "\nInference output Dequantized (first 10 floats of 6th token): ";
                    for (int i = 0; i < 10; i++)
                    {
                        std::cout << text_embedding[i] << " ";
                    }
                    std::cout << std::endl;

#endif

                    // Copy the output_buffer_guards[0] to npy file for debugging
                    std::vector<float> output_npy(CLIP_TEXT_ENCODER_DIM * 77);
#if USE_OUTPUT_FLOAT32_AUTO_DEQUANTIZE
                    std::memcpy(output_npy.data(), float_ptr, CLIP_TEXT_ENCODER_DIM * 77 * sizeof(float));
#else
                    for (size_t i = 0; i < CLIP_TEXT_ENCODER_DIM * 77; i++)
                    {
                        output_npy[i] = (static_cast<float>(uint8_ptr[i]) - output_quant_info[0].qp_zp) *
                                        output_quant_info[0].qp_scale;
                    }
#endif
                    save_as_npy(output_npy, 1, 77, CLIP_TEXT_ENCODER_DIM, "h15_output_embedding_before_projection.npy");
                    std::cout << "Saved output tensor to h15_output_embedding_before_projection.npy" << std::endl;

                    // Load text projection weights and bias
                    Matrix text_projection_weights;
                    if (!text_projection_weights.load("/data/clip_vit_b32_text_projection_weights.bin"))
                    {
                        std::cerr << "Failed to load text projection weights" << std::endl;
                        return;
                    }

                    Matrix text_projection_bias;
                    if (!text_projection_bias.load("/data/clip_vit_b32_text_projection_bias.bin"))
                    {
                        std::cerr << "Failed to load text projection bias" << std::endl;
                        return;
                    }

                    // Apply text projection
                    std::vector<float> projected_embedding =
                        apply_text_projection(text_embedding, text_projection_weights, text_projection_bias);

                    std::cout << "\nFinal text embedding (first 10 floats before l2 norm): ";
                    for (int i = 0; i < 10; i++)
                    {
                        std::cout << projected_embedding[i] << " ";
                    }
                    std::cout << std::endl;

                    // L2 normalize the projected embedding
                    l2_normalize(projected_embedding);

                    std::cout << "\nFinal text embedding (first 10 floats after l2 norm): ";
                    for (int i = 0; i < 10; i++)
                    {
                        std::cout << projected_embedding[i] << " ";
                    }
                    std::cout << std::endl;
                })
            .expect("Failed to start async infer job");

    job.wait(std::chrono::milliseconds(1000));
    std::cout << "Inference completed successfully" << std::endl;
}

int main()
{
    // Load embedding table
    EmbeddingTable table;
    std::string embedding_path = EMBEDDING_LOOKUP_PATH;
    if (!table.load(embedding_path))
    {
        std::cerr << "Failed to load embeddings: " << embedding_path << std::endl;
        return 1;
    }
    std::cout << "Loaded embedding table: " << table.rows << " tokens x " << table.cols << " dims\n";

    // Example: tokens from Hugging Face tokenizer C++ wrapper
    // Suppose tokenizer.encode("a photo of a cat") -> token IDs
    std::vector<int> token_ids = {320, 1125, 539, 320, 2368}; // a photo of a cat (example)

    // Collect embeddings, start and end token will automatically be inserted
    // The build_hailo_input_tensor will pad the necessary token to a fixed length of 77
    std::vector<float> sentence_embedding = build_hailo_input_tensor(token_ids, table);

    std::cout << "Embedding lookup size: " << sentence_embedding.size() << " floats\n";

    // Print first 10 embedding vector
    std::cout << "First 10 values of the embedding lookup vector:" << std::endl;
    for (size_t i = 0; i < 10; i++)
    {
        std::cout << sentence_embedding[i] << " ";
    }
    std::cout << "\n";

    save_as_npy(sentence_embedding, 1, 77, table.cols, "sentence_embedding.npy");
    std::cout << "Saved input tensor to sentence_embedding.npy" << std::endl;

    run_inference(sentence_embedding);

    std::cout << "Program completed successfully" << std::endl;

    return 0;
}
