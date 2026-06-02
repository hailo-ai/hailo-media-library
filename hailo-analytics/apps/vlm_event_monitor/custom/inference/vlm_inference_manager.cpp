#include "vlm_inference_manager.hpp"

#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

// FNV-1a 64-bit hash — fingerprint input buffers so we can tell across runs
// whether identical inputs really produced identical bytes.
static uint64_t fnv1a_hash(const uint8_t *data, size_t length)
{
    static constexpr uint64_t FNV_OFFSET = 0xcbf29ce484222325ULL;
    static constexpr uint64_t FNV_PRIME = 0x100000001b3ULL;
    uint64_t hash = FNV_OFFSET;
    for (size_t index = 0; index < length; index++)
    {
        hash ^= data[index];
        hash *= FNV_PRIME;
    }
    return hash;
}

// Escape a string for embedding as a JSON string literal value.
static std::string json_escape(const std::string &input)
{
    std::string output;
    output.reserve(input.size());
    for (char character : input)
    {
        switch (character)
        {
        case '"':
            output += "\\\"";
            break;
        case '\\':
            output += "\\\\";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            output += character;
            break;
        }
    }
    return output;
}

// ── Factory ─────────────────────────────────────────────────────────────────

tl::expected<std::shared_ptr<VlmInferenceManager>, std::string> VlmInferenceManager::create(const VlmConfig &config)
{
    auto instance = std::shared_ptr<VlmInferenceManager>(new VlmInferenceManager());
    instance->m_config = config;

    // Create VDevice
    hailo_vdevice_params_t vdevice_params = {};
    hailo_init_vdevice_params(&vdevice_params);
    vdevice_params.group_id = config.group_id.c_str();
    vdevice_params.multi_process_service = true;

    std::cout << "Creating VDevice (group: " << config.group_id << ")..." << std::endl;
    auto vdevice_exp = hailort::VDevice::create(vdevice_params);
    if (!vdevice_exp)
    {
        return tl::make_unexpected("Failed to create VDevice, status = " +
                                   std::to_string(static_cast<int>(vdevice_exp.status())));
    }
    instance->m_vdevice = std::move(vdevice_exp.value());

    // Create VLM from HEF
    std::cout << "Loading VLM from HEF: " << config.hef_path << std::endl;
    auto vlm_params = hailort::genai::VLMParams(config.hef_path);
    auto vlm_exp = hailort::genai::VLM::create(instance->m_vdevice, vlm_params);
    if (!vlm_exp)
    {
        return tl::make_unexpected("Failed to create VLM from HEF: " + config.hef_path +
                                   ", status = " + std::to_string(static_cast<int>(vlm_exp.status())));
    }
    instance->m_vlm = std::make_unique<hailort::genai::VLM>(std::move(vlm_exp.value()));

    // Create default generator params
    auto gen_params_exp = instance->m_vlm->create_generator_params();
    if (!gen_params_exp)
    {
        return tl::make_unexpected("Failed to create generator params, status = " +
                                   std::to_string(static_cast<int>(gen_params_exp.status())));
    }
    instance->m_generator_params.emplace(std::move(gen_params_exp.value()));

    auto status = instance->m_generator_params->set_max_generated_tokens(config.default_max_generated_tokens);
    if (HAILO_SUCCESS != status)
    {
        return tl::make_unexpected("Failed to set max_generated_tokens, status = " +
                                   std::to_string(static_cast<int>(status)));
    }

    // Greedy decoding — identical inputs produce identical outputs. Short-circuits
    // temperature / top-p / top-k. Required for reproducible event-detection runs.
    status = instance->m_generator_params->set_do_sample(false);
    if (HAILO_SUCCESS != status)
    {
        return tl::make_unexpected("Failed to set do_sample=false, status = " +
                                   std::to_string(static_cast<int>(status)));
    }

    // Query input frame shape
    const auto &shape = instance->m_vlm->input_frame_shape();
    instance->m_input_height = shape.height;
    instance->m_input_width = shape.width;
    instance->m_input_channels = shape.features;

    std::cout << "VLM initialized. Input shape: " << shape.height << "x" << shape.width << "x" << shape.features << " ("
              << instance->m_vlm->input_frame_size() << " bytes). Max tokens: " << config.default_max_generated_tokens
              << std::endl;

    instance->m_initialized = true;
    return instance;
}

// ── Lock Acquisition ────────────────────────────────────────────────────────

tl::expected<std::unique_lock<std::timed_mutex>, std::string> VlmInferenceManager::acquire_lock()
{
    std::unique_lock<std::timed_mutex> lock(m_infer_mutex, std::defer_lock);
    if (!lock.try_lock_for(m_config.busy_wait_timeout))
    {
        return tl::make_unexpected("VLM busy: timed out after " + std::to_string(m_config.busy_wait_timeout.count()) +
                                   "ms waiting for inference lock");
    }
    return lock;
}

// ── Session Management ──────────────────────────────────────────────────────

tl::expected<uint32_t, std::string> VlmInferenceManager::create_session()
{
    std::lock_guard<std::mutex> lock(m_session_mutex);

    uint32_t session_id = m_next_session_id++;
    SessionState state;
    state.session_id = session_id;
    state.first_inference = true;
    m_sessions[session_id] = std::move(state);

    std::cout << "Session " << session_id << " created" << std::endl;
    return session_id;
}

tl::expected<void, std::string> VlmInferenceManager::close_session(uint32_t session_id)
{
    // Acquire inference lock to safely clear context if this session is active
    auto lock_result = acquire_lock();
    if (!lock_result)
    {
        return tl::make_unexpected(lock_result.error());
    }
    std::lock_guard<std::mutex> session_lock(m_session_mutex);

    auto iter = m_sessions.find(session_id);
    if (iter == m_sessions.end())
    {
        return tl::make_unexpected("Session " + std::to_string(session_id) + " does not exist");
    }

    // If this session is currently active on device, clear it
    if (m_active_session_id == session_id)
    {
        m_vlm->clear_context();
        m_active_session_id = 0;
    }

    m_sessions.erase(iter);
    std::cout << "Session " << session_id << " closed" << std::endl;
    return {};
}

std::vector<uint32_t> VlmInferenceManager::list_sessions() const
{
    std::lock_guard<std::mutex> lock(m_session_mutex);

    std::vector<uint32_t> ids;
    ids.reserve(m_sessions.size());
    for (const auto &[id, _] : m_sessions)
    {
        ids.push_back(id);
    }
    return ids;
}

// ── Context Switching ───────────────────────────────────────────────────────

tl::expected<void, std::string> VlmInferenceManager::suspend_active_session()
{
    if (m_active_session_id == 0)
    {
        return {};
    }

    auto iter = m_sessions.find(m_active_session_id);
    if (iter == m_sessions.end())
    {
        // Session was closed while active — just clear
        m_vlm->clear_context();
        m_active_session_id = 0;
        return {};
    }

    // Save context
    auto ctx_exp = m_vlm->save_context();
    if (!ctx_exp)
    {
        return tl::make_unexpected("Failed to save context for session " + std::to_string(m_active_session_id) +
                                   ", status = " + std::to_string(static_cast<int>(ctx_exp.status())));
    }
    iter->second.saved_context = std::move(ctx_exp.value());

    std::cout << "Session " << m_active_session_id << " context saved (" << iter->second.saved_context->size()
              << " bytes)" << std::endl;

    m_vlm->clear_context();
    m_active_session_id = 0;
    return {};
}

tl::expected<void, std::string> VlmInferenceManager::activate_session(uint32_t session_id)
{
    // Already active — nothing to do
    if (m_active_session_id == session_id)
    {
        return {};
    }

    auto iter = m_sessions.find(session_id);
    if (iter == m_sessions.end())
    {
        return tl::make_unexpected("Session " + std::to_string(session_id) + " does not exist");
    }

    // Suspend whatever is currently on device
    auto suspend_result = suspend_active_session();
    if (!suspend_result)
    {
        return suspend_result;
    }

    // Restore target session if it has saved context
    if (iter->second.saved_context)
    {
        std::cout << "Restoring session " << session_id << " context..." << std::endl;
        auto status = m_vlm->load_context(hailort::MemoryView(*iter->second.saved_context));
        if (HAILO_SUCCESS != status)
        {
            return tl::make_unexpected("Failed to restore context for session " + std::to_string(session_id) +
                                       ", status = " + std::to_string(static_cast<int>(status)));
        }
        iter->second.saved_context.reset();
    }
    // else: fresh session, no context to restore — device is already clear

    m_active_session_id = session_id;
    return {};
}

// ── Message Building ────────────────────────────────────────────────────────

std::vector<std::string> VlmInferenceManager::build_messages(const InferenceRequest &request,
                                                             bool include_system_prompt)
{
    std::vector<std::string> messages;

    // System prompt (only on first inference of a session)
    if (include_system_prompt && !request.system_prompt.empty())
    {
        messages.push_back(R"({"role": "system", "content": ")" + json_escape(request.system_prompt) + R"("})");
    }

    // User message
    if (!request.frames.empty())
    {
        // Build content array with image placeholders + text
        std::string content_array;
        for (size_t i = 0; i < request.frames.size(); i++)
        {
            if (i > 0)
            {
                content_array += ", ";
            }
            if (request.use_video_mode)
            {
                // In video mode, all frames belong to one video — single placeholder
                if (i == 0)
                {
                    content_array += R"({"type": "video"})";
                }
                // Only first frame gets the video placeholder; remaining frames
                // are part of the same video and don't need individual placeholders
            }
            else
            {
                content_array += R"({"type": "image"})";
            }
        }

        // For video mode, we only have one placeholder regardless of frame count
        if (request.use_video_mode)
        {
            content_array = R"({"type": "video"})";
        }

        content_array += R"(, {"type": "text", "text": ")" + json_escape(request.prompt) + R"("})";

        messages.push_back(R"({"role": "user", "content": [)" + content_array + R"(]})");
    }
    else
    {
        // Text-only follow-up
        messages.push_back(R"({"role": "user", "content": ")" + json_escape(request.prompt) + R"("})");
    }

    return messages;
}

// ── Core Generate Loop ──────────────────────────────────────────────────────

tl::expected<InferenceResult, std::string> VlmInferenceManager::run_generate(const InferenceRequest &request,
                                                                             bool include_system_prompt,
                                                                             TokenCallback on_token)
{
    // Set max tokens for this call
    uint32_t tokens_to_set =
        (request.max_generated_tokens > 0) ? request.max_generated_tokens : m_config.default_max_generated_tokens;
    auto set_status = m_generator_params->set_max_generated_tokens(tokens_to_set);
    if (HAILO_SUCCESS != set_status)
    {
        return tl::make_unexpected("Failed to set max_generated_tokens = " + std::to_string(tokens_to_set));
    }

    // Validate caller-supplied preprocessed frames match the model's expected
    // input layout, then copy into device-addressable buffers. Preprocessing
    // (JPEG decode, letterbox, BGR->RGB) is the caller's responsibility — see
    // VlmFramePreprocessor.
    std::vector<hailort::BufferPtr> frame_buffers;
    const auto frame_size = m_vlm->input_frame_size();

    for (size_t frame_index = 0; frame_index < request.frames.size(); frame_index++)
    {
        const auto &rgb = request.frames[frame_index];
        if (rgb.size() != frame_size)
        {
            return tl::make_unexpected("Frame " + std::to_string(frame_index) + " has size " +
                                       std::to_string(rgb.size()) + " bytes, expected " + std::to_string(frame_size) +
                                       " (H*W*C). "
                                       "Did you run the JPEG through VlmFramePreprocessor?");
        }
        auto buf = hailort::Buffer::create_shared(frame_size).expect("Failed to allocate frame buffer");
        std::memcpy(buf->data(), rgb.data(), frame_size);
        frame_buffers.push_back(std::move(buf));
    }

    // Build MemoryView vectors
    std::vector<hailort::MemoryView> input_frames;
    std::vector<std::vector<hailort::MemoryView>> input_videos;

    if (request.use_video_mode && !frame_buffers.empty())
    {
        std::vector<hailort::MemoryView> video_frames;
        for (auto &buf : frame_buffers)
        {
            video_frames.push_back(hailort::MemoryView(*buf));
        }
        input_videos.push_back(std::move(video_frames));
    }
    else
    {
        for (auto &buf : frame_buffers)
        {
            input_frames.push_back(hailort::MemoryView(*buf));
        }
    }

    // Build messages
    auto messages = build_messages(request, include_system_prompt);

    // ── HailoRT payload log ─────────────────────────────────────────────────
    // Fingerprint every field that becomes part of m_vlm->generate() so two
    // "identical" runs can be compared byte-for-byte. If hashes and messages
    // match across runs but output differs, the non-determinism is in the HEF
    // / sampling / device state, not in this app.
    std::cout << "──── HailoRT generate() payload ────" << std::endl;
    std::cout << "  max_generated_tokens : " << tokens_to_set << std::endl;
    std::cout << "  include_system_prompt: " << (include_system_prompt ? "true" : "false") << std::endl;
    std::cout << "  use_video_mode       : " << (request.use_video_mode ? "true" : "false") << std::endl;
    std::cout << "  frame_count          : " << frame_buffers.size() << std::endl;
    for (size_t frame_index = 0; frame_index < frame_buffers.size(); frame_index++)
    {
        const auto &buf = frame_buffers[frame_index];
        uint64_t hash = fnv1a_hash(static_cast<const uint8_t *>(buf->data()), buf->size());
        std::cout << "  frame[" << frame_index << "]             : size=" << buf->size() << " bytes  fnv1a=0x"
                  << std::hex << std::setw(16) << std::setfill('0') << hash << std::dec << std::setfill(' ')
                  << std::endl;
    }
    std::cout << "  messages (" << messages.size() << " entries):" << std::endl;
    for (size_t message_index = 0; message_index < messages.size(); message_index++)
    {
        std::cout << "    [" << message_index << "] " << messages[message_index] << std::endl;
    }
    std::cout << "────────────────────────────────────" << std::endl;

    // Start generation
    auto t_start = std::chrono::steady_clock::now();
    auto completion_exp = m_vlm->generate(*m_generator_params, messages, input_frames, input_videos);
    if (!completion_exp)
    {
        return tl::make_unexpected("Failed to generate, status = " +
                                   std::to_string(static_cast<int>(completion_exp.status())));
    }
    auto completion = std::move(completion_exp.value());

    // Read tokens
    bool is_first_token = true;
    auto t_first_token = t_start;
    size_t context_after_first_token = 0;
    std::ostringstream full_response;

    while (hailort::genai::LLMGeneratorCompletion::Status::GENERATING == completion.generation_status())
    {
        auto output_exp = completion.read();
        if (!output_exp)
        {
            return tl::make_unexpected("Token read failed, status = " +
                                       std::to_string(static_cast<int>(output_exp.status())));
        }
        auto output = std::move(output_exp.value());

        if (is_first_token)
        {
            t_first_token = std::chrono::steady_clock::now();
            is_first_token = false;
            auto ctx_exp = m_vlm->get_context_usage_size();
            if (ctx_exp)
            {
                context_after_first_token = ctx_exp.value();
            }
        }

        // Strip end-of-sequence marker
        auto eos_pos = output.find("<|im_end|>");
        if (eos_pos != std::string::npos)
        {
            output.erase(eos_pos);
        }

        if (!output.empty())
        {
            full_response << output;
            if (on_token && !on_token(output))
            {
                break;
            }
        }
    }

    auto t_end = std::chrono::steady_clock::now();

    // Compute stats
    InferenceResult result;
    result.response = full_response.str();

    double ttft_ms = std::chrono::duration<double, std::milli>(t_first_token - t_start).count();
    double total_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    double tf_ms = total_ms - ttft_ms;

    size_t context_after = 0;
    auto ctx_exp = m_vlm->get_context_usage_size();
    if (ctx_exp)
    {
        context_after = ctx_exp.value();
    }
    size_t tokens_generated =
        (context_after > context_after_first_token) ? (context_after - context_after_first_token) : 0;

    double tps =
        (tf_ms > 0.0 && tokens_generated > 0) ? (static_cast<double>(tokens_generated) / (tf_ms / 1000.0)) : 0.0;

    result.stats.ttft_ms = ttft_ms;
    result.stats.tf_ms = tf_ms;
    result.stats.total_ms = total_ms;
    result.stats.tps = tps;
    result.stats.tokens_generated = tokens_generated;
    result.stats.context_usage = context_after;

    auto capacity_exp = m_vlm->max_context_capacity();
    if (capacity_exp)
    {
        result.stats.context_capacity = capacity_exp.value();
    }

    return result;
}

// ── Public Inference Methods ────────────────────────────────────────────────

tl::expected<InferenceResult, std::string> VlmInferenceManager::infer(uint32_t session_id,
                                                                      const InferenceRequest &request,
                                                                      TokenCallback on_token)
{
    auto lock_result = acquire_lock();
    if (!lock_result)
    {
        return tl::make_unexpected(lock_result.error());
    }

    if (!m_initialized || !m_vlm)
    {
        return tl::make_unexpected("VLM is not initialized");
    }

    if (request.prompt.empty())
    {
        return tl::make_unexpected("Prompt cannot be empty");
    }

    // Look up session under session_mutex
    bool is_first = false;
    {
        std::lock_guard<std::mutex> session_lock(m_session_mutex);
        auto iter = m_sessions.find(session_id);
        if (iter == m_sessions.end())
        {
            return tl::make_unexpected("Session " + std::to_string(session_id) + " does not exist");
        }
        is_first = iter->second.first_inference;
    }

    if (is_first && request.frames.empty())
    {
        return tl::make_unexpected("First inference in a session must include at least one frame");
    }

    // Activate this session (save/restore context as needed)
    auto activate_result = activate_session(session_id);
    if (!activate_result)
    {
        return tl::make_unexpected(activate_result.error());
    }

    try
    {
        auto result = run_generate(request, is_first, on_token);
        if (result)
        {
            std::lock_guard<std::mutex> session_lock(m_session_mutex);
            auto iter = m_sessions.find(session_id);
            if (iter != m_sessions.end())
            {
                iter->second.first_inference = false;
            }
        }
        return result;
    }
    catch (const std::exception &e)
    {
        return tl::make_unexpected(std::string("Inference error: ") + e.what());
    }
}

tl::expected<InferenceResult, std::string> VlmInferenceManager::infer_oneshot(const InferenceRequest &request,
                                                                              TokenCallback on_token)
{
    auto lock_result = acquire_lock();
    if (!lock_result)
    {
        return tl::make_unexpected(lock_result.error());
    }

    if (!m_initialized || !m_vlm)
    {
        return tl::make_unexpected("VLM is not initialized");
    }

    if (request.prompt.empty())
    {
        return tl::make_unexpected("Prompt cannot be empty");
    }

    if (request.frames.empty())
    {
        return tl::make_unexpected("One-shot inference must include at least one frame");
    }

    // Suspend any active session (will be restored lazily on next infer())
    auto suspend_result = suspend_active_session();
    if (!suspend_result)
    {
        return tl::make_unexpected(suspend_result.error());
    }

    m_vlm->clear_context();

    try
    {
        bool include_system_prompt = !request.system_prompt.empty();
        auto result = run_generate(request, include_system_prompt, on_token);
        m_vlm->clear_context();
        return result;
    }
    catch (const std::exception &e)
    {
        m_vlm->clear_context();
        return tl::make_unexpected(std::string("Inference error: ") + e.what());
    }
}

// ── Info ────────────────────────────────────────────────────────────────────

tl::expected<size_t, std::string> VlmInferenceManager::get_context_usage()
{
    auto lock_result = acquire_lock();
    if (!lock_result)
    {
        return tl::make_unexpected(lock_result.error());
    }

    if (!m_vlm)
    {
        return tl::make_unexpected("VLM is not initialized");
    }

    auto exp = m_vlm->get_context_usage_size();
    if (!exp)
    {
        return tl::make_unexpected("Failed to get context usage, status = " +
                                   std::to_string(static_cast<int>(exp.status())));
    }
    return exp.value();
}

tl::expected<size_t, std::string> VlmInferenceManager::max_context_capacity()
{
    auto lock_result = acquire_lock();
    if (!lock_result)
    {
        return tl::make_unexpected(lock_result.error());
    }

    if (!m_vlm)
    {
        return tl::make_unexpected("VLM is not initialized");
    }

    auto exp = m_vlm->max_context_capacity();
    if (!exp)
    {
        return tl::make_unexpected("Failed to get max context capacity, status = " +
                                   std::to_string(static_cast<int>(exp.status())));
    }
    return exp.value();
}
