#include "text_encoder.hpp"

#include <utility>

TextEncoder::EncoderResult::EncoderResult(const std::string &id, std::map<std::string, std::vector<float>> positive,
                                          std::map<std::string, std::vector<float>> negative)
    : network_id(id), positive_embeddings(std::move(positive)), negative_embeddings(std::move(negative))
{
}

std::string TextEncoder::error_to_string(ErrorCode error) const
{
    switch (error)
    {
    case ErrorCode::SUCCESS:
        return "Success";
    case ErrorCode::INVALID_PARAMETER:
        return "Invalid parameter";
    case ErrorCode::UNINITIALIZED:
        return "Encoder not initialized";
    case ErrorCode::SERVICE_ERROR:
        return "Service error";
    case ErrorCode::ENCODING_ERROR:
        return "Encoding error";
    case ErrorCode::TOKENIZATION_ERROR:
        return "Tokenization error";
    case ErrorCode::TIMEOUT:
        return "Operation timeout";
    case ErrorCode::UNKNOWN_ERROR:
    default:
        return "Unknown error";
    }
}
