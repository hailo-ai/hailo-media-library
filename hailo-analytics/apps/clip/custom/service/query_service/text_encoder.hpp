#pragma once

#include <string>
#include <vector>
#include <map>
#include <tl/expected.hpp>

/**
 * Base class for text encoders
 * Provides a common interface for different text encoding implementations
 */
class TextEncoder
{
  public:
    // Base error codes that can be extended by derived classes
    enum class ErrorCode
    {
        SUCCESS,
        INVALID_PARAMETER,
        UNINITIALIZED,
        SERVICE_ERROR,
        ENCODING_ERROR,
        TOKENIZATION_ERROR,
        TIMEOUT,
        UNKNOWN_ERROR
    };

    // Base result structure for text encoding
    struct EncoderResult
    {
        std::string network_id;
        std::map<std::string, std::vector<float>> positive_embeddings; // Map of prompt to embedding vector
        std::map<std::string, std::vector<float>> negative_embeddings; // Map of prompt to embedding vector

        EncoderResult() = default;

        EncoderResult(const std::string &id, std::map<std::string, std::vector<float>> positive,
                      std::map<std::string, std::vector<float>> negative)
            : network_id(id), positive_embeddings(std::move(positive)), negative_embeddings(std::move(negative))
        {
        }
    };

    // Virtual destructor for proper cleanup of derived classes
    virtual ~TextEncoder() = default;

    // Disable copy construction and assignment
    TextEncoder(const TextEncoder &) = delete;
    TextEncoder &operator=(const TextEncoder &) = delete;

    // Allow move construction and assignment
    TextEncoder(TextEncoder &&) = default;
    TextEncoder &operator=(TextEncoder &&) = default;

    /**
     * Initialize the text encoder
     * @return Expected void on success, ErrorCode on failure
     */
    virtual tl::expected<void, ErrorCode> initialize() = 0;

    /**
     * Encode text prompts into embeddings
     * @param network_id Identifier for the network/model to use
     * @param positive_prompts List of positive text prompts to encode
     * @param negative_prompts List of negative text prompts to encode (optional)
     * @return Expected EncoderResult on success, ErrorCode on failure
     */
    virtual tl::expected<EncoderResult, ErrorCode> encode_text(
        const std::string &network_id, const std::vector<std::string> &positive_prompts,
        const std::vector<std::string> &negative_prompts = {}) = 0;

    /**
     * Check if the encoder is initialized
     * @return true if initialized, false otherwise
     */
    virtual bool is_initialized() const = 0;

    /**
     * Get supported network IDs
     * @return Vector of supported network identifiers
     */
    virtual std::vector<std::string> get_supported_networks() const = 0;

  protected:
    // Protected default constructor - only derived classes can construct
    TextEncoder() = default;

    /**
     * Helper function to convert error codes to string descriptions
     * Can be overridden by derived classes for custom error messages
     */
    virtual std::string error_to_string(ErrorCode error) const
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
};
