/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <nlohmann/json.hpp>

#include "clip.hpp"
#include <zmq.hpp>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include "hailo_postprocess_tools/objects/hailo_common.hpp"
#include "hailo_postprocess_tools/tracking/hailo_tracker.hpp"

const char *output_layer_name = "clip_resnet_50/conv59";
bool initialization_done = false;
std::queue<std::vector<std::vector<float>>> m_text_embedding_queue;
std::vector<std::string> prompts;
std::vector<std::vector<float>> text_embeddings;
std::vector<bool> negatives;
float threshold = 0.0;

std::vector<float> probs;

float logit_scale = std::exp(4.60517);

std::mutex image_queue_mutex;
std::mutex text_queue_mutex;

using json = nlohmann::json;
/**
 * @brief Receive and process messages from the publisher using ZeroMQ SUB socket.
 *
 * @param subscriber The ZeroMQ subscriber socket.
 */
void decode_zmq_messages(const std::string &message_str)
{
    if (message_str.empty())
        return;

    json received_json = json::parse(message_str, /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (received_json.is_discarded())
    {
        std::cerr << "[CLIP] bad json, skip\n";
        return;
    }

    if (received_json.contains("prompts") && received_json["prompts"].is_array())
    {
        prompts.clear();
        for (const auto &item : received_json["prompts"])
            prompts.push_back(item.is_null() ? "" : item.get<std::string>());
    }

    if (received_json.contains("embedding") && received_json["embedding"].is_array())
    {
        text_embeddings.clear();
        for (const auto &item : received_json["embedding"])
            text_embeddings.push_back(item.is_null() ? std::vector<float>{} : item.get<std::vector<float>>());
    }

    if (received_json.contains("negatives") && received_json["negatives"].is_array())
    {
        negatives.clear();
        for (const auto &item : received_json["negatives"])
            negatives.push_back(item.is_null() ? false : item.get<bool>());
    }

    if (received_json.contains("threshold"))
        threshold = received_json["threshold"].get<float>();

    {
        std::lock_guard<std::mutex> lg(text_queue_mutex);
        m_text_embedding_queue.push(text_embeddings);
    }
}

/**
 * @brief Send results using ZeroMQ PUB socket.
 *
 * @param result A pair of integers representing the result.
 */
void send_results_to_roi(HailoROIPtr roi, int track_id, int index, const std::vector<int> &online_detection_ids)
{
    // Calculate the total size: 2 ints for track_id and index, 1 int for the vector size,
    // and the size of the vector of ints (online_detection_ids)
    size_t size = 2 * sizeof(int) + sizeof(int) + online_detection_ids.size() * sizeof(int);

    zmq::message_t msg(size);

    int *data = static_cast<int *>(msg.data());

    // Fill in track_id and index (first 8 bytes)
    data[0] = track_id;
    data[1] = index;

    // Fill in the vector size (next 4 bytes)
    data[2] = online_detection_ids.size();

    // Fill in the online detection IDs
    std::memcpy(data + 3, online_detection_ids.data(), online_detection_ids.size() * sizeof(int));

    // Remove any existing HailoZMQ objects from the ROI
    for (const auto &obj : roi->get_objects())
    {
        if (obj->get_type() != HAILO_ZMQ)
            continue;

        auto z = std::dynamic_pointer_cast<HailoZMQMessage>(obj);
        if (z && z->get_input_msg().empty())
            roi->remove_object(obj);
    }

    // Create a new HailoZMQMessage object and set the output message
    auto zmq_msg = std::make_shared<HailoZMQMessage>();
    std::string out_json(static_cast<char *>(msg.data()), msg.size());
    zmq_msg->set_output_msg(std::move(out_json));
    roi->add_object(zmq_msg);
}

/**
 * @brief Apply the softmax function to a vector of logits.
 *
 * @param logits A vector of logits.
 * @return A vector of probabilities.
 */
std::vector<float> softmax(const std::vector<float> &logits)
{
    std::vector<float> exp_logits(logits.size());
    float max_logit = *std::max_element(logits.begin(), logits.end());

    float sum_exp = 0.0;
    for (size_t i = 0; i < logits.size(); ++i)
    {
        if (logits[i] == 0.0f)
        {
            continue;
        }
        exp_logits[i] = std::exp(logits[i] - max_logit);
        sum_exp += exp_logits[i];
    }

    for (size_t i = 0; i < exp_logits.size(); ++i)
    {
        exp_logits[i] /= sum_exp;
    }

    return exp_logits;
}

/**
 * @brief Normalize a vector.
 *
 * @param vec A vector to be normalized.
 */
void normalize(std::vector<float> &vec)
{
    float norm = std::sqrt(std::inner_product(vec.begin(), vec.end(), vec.begin(), 0.0f));

    if (norm != 0.0f)
    {
        std::transform(vec.begin(), vec.end(), vec.begin(), [norm](float v) { return v / norm; });
    }
}

/**
 * @brief Normalize a 2D vector.
 *
 * @param data A 2D vector to be normalized.
 */
void normalize_vectors(std::vector<std::vector<float>> &data)
{
    for (auto &vec : data)
    {
        float norm = std::sqrt(
            std::accumulate(vec.begin(), vec.end(), 0.0f, [](float sum, float val) { return sum + val * val; }));

        if (norm == 0.0f)
        {
            continue;
        }

        for (auto &val : vec)
        {
            val /= norm;
        }
    }
}

/**
 * @brief Compute the dot product between a vector and a 2D vector.
 *
 * @param A A vector.
 * @param B A 2D vector.
 * @return A vector of dot product results.
 */
std::vector<float> custom_dot_product(const std::vector<float> &A, const std::vector<std::vector<float>> &B)
{
    std::vector<float> result(B.size());

    for (std::size_t i = 0; i < B.size(); ++i)
    {
        if (B[i].size() == 0)
        {
            result[i] = 0.0f;
        }
        else
        {
            result[i] = std::inner_product(A.begin(), A.end(), B[i].begin(), 0.0f);
            result[i] = result[i] * logit_scale;
        }
    }

    return result;
}

/**
 * @brief Calculate probabilities and send them.
 *
 * This function normalizes the image embeddings and text embeddings,
 * computes the dot product between them, and then applies the softmax
 * function to get the probabilities.
 *
 * @param text_embeddings A 2D vector containing text embeddings.
 * @param image_embeddings A vector containing image embeddings.
 */
void calc_and_send_probs(std::vector<std::vector<float>> &text_embeddings, std::vector<float> &image_embeddings)
{
    normalize(image_embeddings);
    normalize_vectors(text_embeddings);

    std::vector<float> dot_product_result = custom_dot_product(image_embeddings, text_embeddings);

    probs = softmax(dot_product_result);
}

/**
 * @brief Get the image embedding and push it to the image embedding queue.
 *
 * This function retrieves the tensor from the given ROI, dequantizes the
 * tensor data, and returns it as a vector of floats.
 *
 * @param roi A pointer to the region of interest (ROI).
 * @return A vector of floats representing the dequantized image embedding.
 */
std::vector<float> get_image_embedding(HailoROIPtr roi)
{
    HailoTensorPtr tensor = roi->get_tensor(output_layer_name);
    if (tensor)
    {
        std::unique_lock<std::mutex> lock(image_queue_mutex);
        uint8_t *data_ptr = tensor->data();
        size_t data_size = tensor->size();

        std::vector<float> dequantized_data(data_size);
        // Dequantize the tensor data
        for (size_t i = 0; i < data_size; ++i)
        {
            dequantized_data[i] = tensor->fix_scale(data_ptr[i]);
        }
        return dequantized_data;
    }

    return {};
}

/**
 * @brief Get the unique tracking ID from a detection.
 *
 * This function retrieves the unique tracking ID from the given detection.
 *
 * @param detection A pointer to the detection object.
 * @return A pointer to the unique tracking ID.
 */
HailoUniqueIDPtr get_tracking_id(HailoDetectionPtr detection)
{
    for (auto obj : detection->get_objects_typed(HAILO_UNIQUE_ID))
    {
        HailoUniqueIDPtr id = std::dynamic_pointer_cast<HailoUniqueID>(obj);
        if (id->get_mode() == TRACKING_ID)
        {
            return id;
        }
    }
    return nullptr;
}

/**
 * @brief Process the ROI using the CLIP model.
 *
 * @param roi A pointer to the region of interest (ROI).
 */
void clip(HailoROIPtr roi)
{
    // Get zmq_msg from ROI
    HailoZMQMessagePtr zmq_msg = nullptr;
    for (const auto &obj : roi->get_objects())
    {
        if (obj->get_type() != HAILO_ZMQ)
            continue;

        auto z = std::dynamic_pointer_cast<HailoZMQMessage>(obj);
        if (z && !z->get_input_msg().empty())
        {
            zmq_msg = z;
            break;
        }
    }

    if (!zmq_msg)
    {
        return;
    }

    // Decode
    std::string input_json = zmq_msg->get_input_msg();
    decode_zmq_messages(input_json);

    // Compute
    std::vector<float> image_embedding = get_image_embedding(roi);
    calc_and_send_probs(text_embeddings, image_embedding);

    if (roi && prompts.size() > 0)
    {
        HailoDetectionPtr detection = std::dynamic_pointer_cast<HailoDetection>(roi);

        int tracking_id = get_tracking_id(detection)->get_id();
        HailoTracker::GetInstance().remove_classifications_from_track("hailo_tracker", tracking_id, "clip");

        std::vector<HailoDetectionPtr> online_detection_ptrs =
            HailoTracker::GetInstance().get_online_stracks("hailo_tracker");
        std::vector<int> online_detection_ids;
        for (auto det : online_detection_ptrs)
        {
            HailoUniqueIDPtr id = get_tracking_id(det);
            if (id)
            {
                online_detection_ids.push_back(id->get_id());
            }
        }

        auto max_prob = std::max_element(probs.begin(), probs.end());
        int index = std::distance(probs.begin(), max_prob);
        std::string label = prompts[index];

        const std::string prefix = "A photo of ";
        if (label.rfind(prefix, 0) == 0)
        {
            label.erase(0, prefix.length());
        }

        if (!negatives[index] && *max_prob > threshold && label != "")
        {
            HailoClassificationPtr classification =
                std::make_shared<HailoClassification>(std::string("clip"), index + 3, label, *max_prob);

            HailoTracker::GetInstance().add_object_to_track("hailo_tracker", tracking_id, classification);
            send_results_to_roi(roi, tracking_id, index, online_detection_ids);
        }
        else
        {
            send_results_to_roi(roi, tracking_id, -1, online_detection_ids);
        }
    }
}

/**
 * @brief Process the ROI using the CLIP ResNet-50 model with NV12 format.
 *
 * @param roi A pointer to the region of interest (ROI).
 */
void clip_resnet_50_nv12(HailoROIPtr roi)
{
    output_layer_name = "clip_resnet_50x4_image_encoder_nv12/conv89";
    clip(roi);
}
