#include <iostream>
#include <fstream>
#include "media_library/cloexec_fstream.hpp"
#include <sstream>
#include <memory>
#include <vector>
#include <string>

#include <tokenizers_cpp.h>

std::string read_file(const std::string &filename)
{
    cloexec::ifstream file(filename);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open file: " + filename);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main(int argc, char *argv[])
{
    try
    {
        // Default path to tokenizer.json - can be overridden by command line argument
        std::string tokenizer_path = "./tokenizer.json";
        if (argc > 1)
        {
            tokenizer_path = argv[1];
        }

        std::cout << "Loading tokenizer from: " << tokenizer_path << std::endl;

        // Read the tokenizer JSON file
        std::string json_content = read_file(tokenizer_path);

        // Create tokenizer from JSON blob
        auto tokenizer = tokenizers::Tokenizer::FromBlobJSON(json_content);
        if (!tokenizer)
        {
            std::cerr << "Failed to create tokenizer from JSON file" << std::endl;
            return 1;
        }

        std::cout << "Tokenizer loaded successfully!" << std::endl;
        std::cout << "Vocabulary size: " << tokenizer->GetVocabSize() << std::endl;

        // Test text samples
        std::vector<std::string> test_texts = {"a photo of a cat", "a beautiful sunset over the ocean",
                                               "a red car driving on the highway", "Hello, world!"};

        std::cout << "\n=== Single Text Encoding Examples ===" << std::endl;
        for (const auto &text : test_texts)
        {
            std::cout << "\nText: \"" << text << "\"" << std::endl;

            // Encode the text
            auto token_ids = tokenizer->Encode(text);

            std::cout << "Token IDs: [";
            for (size_t i = 0; i < token_ids.size(); ++i)
            {
                std::cout << token_ids[i];
                if (i < token_ids.size() - 1)
                    std::cout << ", ";
            }
            std::cout << "]" << std::endl;

            // Decode back to text
            auto decoded_text = tokenizer->Decode(token_ids);
            std::cout << "Decoded: \"" << decoded_text << "\"" << std::endl;
        }

        std::cout << "\n=== Batch Encoding Example ===" << std::endl;
        auto batch_encoded = tokenizer->EncodeBatch(test_texts);
        for (size_t i = 0; i < test_texts.size(); ++i)
        {
            std::cout << "Text " << i << ": \"" << test_texts[i] << "\" -> [";
            for (size_t j = 0; j < batch_encoded[i].size(); ++j)
            {
                std::cout << batch_encoded[i][j];
                if (j < batch_encoded[i].size() - 1)
                    std::cout << ", ";
            }
            std::cout << "]" << std::endl;
        }

        std::cout << "\n=== Token/ID Conversion Examples ===" << std::endl;
        // Test some specific tokens
        std::vector<std::string> tokens_to_test = {"<start_of_text>", "<end_of_text>", "cat", "photo", "a"};
        for (const auto &token : tokens_to_test)
        {
            int32_t token_id = tokenizer->TokenToId(token);
            if (token_id != -1)
            {
                std::string recovered_token = tokenizer->IdToToken(token_id);
                std::cout << "Token: \"" << token << "\" -> ID: " << token_id << " -> Recovered: \"" << recovered_token
                          << "\"" << std::endl;
            }
            else
            {
                std::cout << "Token: \"" << token << "\" -> Not found in vocabulary" << std::endl;
            }
        }

        std::cout << "\nTokenizer example completed successfully!" << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
