#include "post_denoise_filter.hpp"

PostDenoiseFilter::PostDenoiseFilter()
    : m_denoise_element_enabled(false), m_enabled(false), m_running(true), m_post_denoise_config{.enabled = false,
                                                                                                 .sharpness = 5,
                                                                                                 .contrast = 1.0,
                                                                                                 .brightness = 1,
                                                                                                 .saturation_u_a = 1.0,
                                                                                                 .saturation_v_a = 1.0,
                                                                                                 .saturation_u_b = 0,
                                                                                                 .saturation_v_b = 0},
      m_denoise_params{.sharpness = 5,
                       .contrast = 1.0,
                       .brightness = 1,
                       .saturation_u_a = 1.0,
                       .saturation_u_b = 0,
                       .saturation_v_a = 1.0,
                       .saturation_v_b = 0},
      m_denoise_update_thread(&PostDenoiseFilter::denoise_read_denoise_params, this)
{
}

PostDenoiseFilter::~PostDenoiseFilter()
{
    m_running = false;
    if (m_denoise_update_thread.joinable())
    {
        m_denoise_update_thread.join();
    }
}

bool PostDenoiseFilter::is_enabled()
{
    return m_enabled;
}

void PostDenoiseFilter::get_denoise_params(dsp_image_enhancement_params_t &denoise_params)
{
    std::shared_lock<std::shared_mutex> lock(m_post_denoise_lock);
    denoise_params = m_denoise_params;
}

void PostDenoiseFilter::denoise_read_denoise_params()
{
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(post_denoise_config_t);
    attr.mq_curmsgs = 0;

    mqd_t mq = mq_open(post_denoise_isp_data.c_str(), O_RDONLY | O_CREAT, 0666, &attr);
    if (mq == (mqd_t)-1)
    {
        LOGGER__ERROR(
            "Error opening message queue named: {}. with the ISP when post denoise filter is enable for reading",
            post_denoise_isp_data);
        return;
    }
    while (m_running)
    {
        LOGGER__TRACE("Reading from the message queue {} from isp", post_denoise_isp_data);
        struct timespec timeout;
        timeout.tv_sec = 0;          // Set the timeout to 0 seconds
        timeout.tv_nsec = 100000000; // Set the timeout to 100 millisecond

        ssize_t bytes_read = mq_timedreceive(mq, reinterpret_cast<char *>(&m_post_denoise_config),
                                             sizeof(post_denoise_config_t), NULL, &timeout);

        if (bytes_read < 0)
        {
            if (errno == ETIMEDOUT)
            {
                continue;
            }
            else
            {
                LOGGER__ERROR("Error receiving post denoise filter data from Isp message");
                break; // Exit the loop and stop the thread
            }
        }
        if (bytes_read < 0)
        {
            LOGGER__ERROR("Error receiving post denoise fillter data from Isp message");
        }
        m_enabled = m_post_denoise_config.enabled;
        // TODO add debug log with the paramaters
        LOGGER__TRACE("post denoise filter parameters received from the ISP: sharpness- {} contrast- {} brightness- {} "
                      "saturation_u_a- {} saturation_u_b- {} saturation_v_a- {} saturation_v_b- {}",
                      m_post_denoise_config.sharpness, m_post_denoise_config.contrast, m_post_denoise_config.brightness,
                      m_post_denoise_config.saturation_u_a, m_post_denoise_config.saturation_u_b,
                      m_post_denoise_config.saturation_v_a, m_post_denoise_config.saturation_v_b);
        if (m_post_denoise_config.sharpness > std::numeric_limits<uint8_t>::max() ||
            m_post_denoise_config.brightness > std::numeric_limits<int16_t>::max() ||
            m_post_denoise_config.saturation_u_b > std::numeric_limits<int16_t>::max() ||
            m_post_denoise_config.saturation_v_b > std::numeric_limits<int16_t>::max() ||
            m_post_denoise_config.sharpness < std::numeric_limits<uint8_t>::min() ||
            m_post_denoise_config.brightness < std::numeric_limits<int16_t>::min() ||
            m_post_denoise_config.saturation_u_b < std::numeric_limits<int16_t>::min() ||
            m_post_denoise_config.saturation_v_b < std::numeric_limits<int16_t>::min())
        {
            LOGGER__WARN("post denoise filter parameters are out of range and will be clamped");
        }
        std::unique_lock<std::shared_mutex> lock(m_post_denoise_lock);
        m_denoise_params = {.sharpness = static_cast<uint8_t>(m_post_denoise_config.sharpness),
                            .contrast = m_post_denoise_config.contrast,
                            .brightness = static_cast<int16_t>(m_post_denoise_config.brightness),
                            .saturation_u_a = m_post_denoise_config.saturation_u_a,
                            .saturation_u_b = static_cast<int16_t>(m_post_denoise_config.saturation_u_b),
                            .saturation_v_a = m_post_denoise_config.saturation_v_a,
                            .saturation_v_b = static_cast<int16_t>(m_post_denoise_config.saturation_v_b)};
    }
    mq_close(mq);
}
