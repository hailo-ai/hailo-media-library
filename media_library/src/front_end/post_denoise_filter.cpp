#include "post_denoise_filter.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <tuple>

PostDenoiseFilter::PostDenoiseFilter()
    : m_denoise_element_enabled(false), m_enabled(false), m_running(true),
      m_post_denoise_config{
          .enabled = false,
          .auto_luma = false,
          .manual_contrast = 1.0,
          .manual_brightness = 0,
          .auto_percentile_low = 2.0,
          .auto_percentile_high = 99.9,
          .auto_target_low = 5,
          .auto_target_high = 248,
          .auto_low_pass_filter_alpha = 0.95,
          .sharpness = 5,
          .saturation = 1.0,
      },
      m_denoise_params{
          .sharpness = 5,
          .contrast = 1.0,
          .brightness = 0,
          .saturation_u_a = 1.0,
          .saturation_u_b = 0,
          .saturation_v_a = 1.0,
          .saturation_v_b = 0,
          .histogram_params = nullptr,
      },
      m_histogram_params{
          .x_sample_step = 29,
          .y_sample_step = 29,
          .histogram = {0},
      },
      m_denoise_update_thread(&PostDenoiseFilter::read_denoise_params_from_isp, this), m_brightness(std::nullopt)
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

dsp_image_enhancement_params_t PostDenoiseFilter::get_dsp_denoise_params()
{
    std::shared_lock<std::shared_mutex> lock(m_post_denoise_lock);
    return m_denoise_params;
}

std::pair<uint16_t, uint16_t> PostDenoiseFilter::histogram_sample_step_for_frame(std::pair<size_t, size_t> frame_size)
{
    auto [width, height] = frame_size;
    float aspect_ratio = width / (float)height;
    int n_height = std::sqrt(PostDenoiseFilter::histogram_sample_size / aspect_ratio);
    int n_width = PostDenoiseFilter::histogram_sample_size / n_height;
    int delta_width = width / n_width + 1;
    int delta_height = height / n_height + 1;

    return std::make_pair(delta_width, delta_height);
}

std::pair<uint8_t, uint8_t> PostDenoiseFilter::find_percentile_pixels(const Histogram &histogram)
{
    // Calculate the total number of pixels
    uint32_t total_pixels = std::accumulate(std::begin(histogram), std::end(histogram), 0);

    // Calculate the target counts for low and high percentiles
    uint32_t target_low_percent =
        static_cast<uint32_t>(total_pixels * m_post_denoise_config.auto_percentile_low / 100.0);
    uint32_t target_high_percent =
        static_cast<uint32_t>(total_pixels * m_post_denoise_config.auto_percentile_high / 100.0);

    // Calculate the cumulative histogram
    std::vector<uint32_t> cumulative_histogram(256);
    std::partial_sum(std::begin(histogram), std::end(histogram), cumulative_histogram.begin());

    // Find the pixel values corresponding to these counts
    auto low_percent_it =
        std::lower_bound(cumulative_histogram.begin(), cumulative_histogram.end(), target_low_percent);
    auto high_percent_it =
        std::lower_bound(cumulative_histogram.begin(), cumulative_histogram.end(), target_high_percent);

    // Convert iterators to indices
    return std::make_pair(std::distance(cumulative_histogram.begin(), low_percent_it),
                          std::distance(cumulative_histogram.begin(), high_percent_it));
}

std::pair<float, float> PostDenoiseFilter::contrast_brightness_lowpass_filter(float contrast, int16_t brightness)
{
    float previous_contrast = m_denoise_params.contrast;
    float previous_brightness = m_brightness.value();
    float new_contrast = m_post_denoise_config.auto_low_pass_filter_alpha * previous_contrast +
                         (1 - m_post_denoise_config.auto_low_pass_filter_alpha) * contrast;
    float new_brightness = m_post_denoise_config.auto_low_pass_filter_alpha * previous_brightness +
                           (1 - m_post_denoise_config.auto_low_pass_filter_alpha) * brightness;
    return std::make_pair(new_contrast, new_brightness);
}

std::pair<float, int16_t> PostDenoiseFilter::contrast_brightness_from_percentiles(uint8_t low_percentile_pixel,
                                                                                  uint8_t high_percentile_pixel)
{
    float b = m_post_denoise_config.auto_target_low;
    float a = static_cast<float>(m_post_denoise_config.auto_target_high - b) /
              (static_cast<float>(high_percentile_pixel - low_percentile_pixel) + 1e-6f);

    float contrast = a;
    int16_t brightness = b - a * low_percentile_pixel;

    return std::make_pair(contrast, brightness);
}

void PostDenoiseFilter::set_dsp_denoise_params_from_histogram(const Histogram &histogram)
{
    auto [low_percentile_pixel, high_percentile_pixel] = find_percentile_pixels(histogram);
    auto [contrast, brightness] = contrast_brightness_from_percentiles(low_percentile_pixel, high_percentile_pixel);
    contrast = std::clamp(contrast, 0.0f, 10.0f);
    brightness = std::clamp(static_cast<int>(brightness), -128, 128);

    std::shared_lock<std::shared_mutex> lock(m_post_denoise_lock);
    // check if we've switched to manual mode in the meantime
    if (!m_denoise_params.histogram_params)
    {
        return;
    }

    float new_contrast, new_brightness;
    if (m_brightness)
    {
        // apply lowpass filter only if we've already sampled a histogram once
        std::tie(new_contrast, new_brightness) = contrast_brightness_lowpass_filter(contrast, brightness);
        LOGGER__TRACE("post denoise filter parameters calcualted from the histogram: "
                      "low percentile pixel {} high percentile pixel {} "
                      "contrast: before low-pass filter + clipping {} after {} "
                      "brightness: before low-pass filter + clipping {} after {}",
                      low_percentile_pixel, high_percentile_pixel, contrast, new_contrast, brightness,
                      static_cast<int16_t>(new_brightness));
    }
    else
    {
        new_contrast = contrast;
        new_brightness = brightness;
        LOGGER__TRACE("post denoise filter parameters calcualted from the histogram: "
                      "low percentile pixel {} high percentile pixel {} "
                      "contrast: {} brightness: {} (clipping without low-pass filter)",
                      low_percentile_pixel, high_percentile_pixel, contrast, brightness);
    }

    m_denoise_params.contrast = new_contrast;
    m_denoise_params.brightness = new_brightness;
    m_brightness = new_brightness;
}

void PostDenoiseFilter::set_dsp_denoise_params_from_isp(const post_denoise_config_t &m_post_denoise_config)
{
    float saturation_a = m_post_denoise_config.saturation;
    int16_t saturation_b = static_cast<int16_t>(128 * (1 - m_post_denoise_config.saturation));

    if (m_post_denoise_config.auto_luma)
    {
        LOGGER__TRACE("post denoise filter parameters received from the ISP: "
                      "auto_luma- {} sharpness- {} saturation - {} "
                      "percentile_low- {} percentile_high- {} target_low- {}  target_high - {} ",
                      m_post_denoise_config.auto_luma, m_post_denoise_config.sharpness,
                      m_post_denoise_config.saturation, m_post_denoise_config.auto_percentile_low,
                      m_post_denoise_config.auto_percentile_high, m_post_denoise_config.auto_target_low,
                      m_post_denoise_config.auto_target_high);

        if (m_post_denoise_config.sharpness > std::numeric_limits<uint8_t>::max())
        {
            LOGGER__WARN("post denoise filter parameters are out of range and will be clamped");
        }

        std::unique_lock<std::shared_mutex> lock(m_post_denoise_lock);
        m_denoise_params.sharpness = static_cast<uint8_t>(m_post_denoise_config.sharpness);
        m_denoise_params.saturation_u_a = saturation_a;
        m_denoise_params.saturation_u_b = saturation_b;
        m_denoise_params.saturation_v_a = saturation_a;
        m_denoise_params.saturation_v_b = saturation_b;
        m_denoise_params.histogram_params = &m_histogram_params;
    }
    else
    {
        LOGGER__TRACE("post denoise filter parameters received from the ISP: "
                      "auto_luma- {} sharpness- {} saturation- {} manual_contrast- {} manual_brightness- {}",
                      m_post_denoise_config.auto_luma, m_post_denoise_config.sharpness,
                      m_post_denoise_config.saturation, m_post_denoise_config.manual_contrast,
                      m_post_denoise_config.manual_brightness);
        if (m_post_denoise_config.sharpness > std::numeric_limits<uint8_t>::max() ||
            m_post_denoise_config.manual_brightness > std::numeric_limits<int16_t>::max())
        {
            LOGGER__WARN("post denoise filter parameters are out of range and will be clamped");
        }

        std::unique_lock<std::shared_mutex> lock(m_post_denoise_lock);
        m_denoise_params.sharpness = static_cast<uint8_t>(m_post_denoise_config.sharpness);
        m_denoise_params.contrast = m_post_denoise_config.manual_contrast;
        m_denoise_params.brightness = static_cast<int16_t>(m_post_denoise_config.manual_brightness);
        m_denoise_params.saturation_u_a = saturation_a;
        m_denoise_params.saturation_u_b = saturation_b;
        m_denoise_params.saturation_v_a = saturation_a;
        m_denoise_params.saturation_v_b = saturation_b;
        m_denoise_params.histogram_params = nullptr;
        m_brightness = std::nullopt;
    }
}

void PostDenoiseFilter::read_denoise_params_from_isp()
{
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(post_denoise_config_t);
    attr.mq_curmsgs = 0;

    mqd_t mq = mq_open(post_denoise_isp_data, O_RDONLY | O_CREAT, 0666, &attr);
    if (mq == (mqd_t)-1)
    {
        LOGGER__ERROR(
            "Error opening message queue named: {}. with the ISP when post denoise filter is enable for reading",
            post_denoise_isp_data);
        return;
    }
    while (m_running)
    {
        struct timespec timeout;
        int ret = clock_gettime(CLOCK_REALTIME, &timeout);
        if (ret != 0)
        {
            LOGGER__ERROR("Failed to get current time: {}", strerror(errno));
            break;
        }

        timeout.tv_sec++; // Set the timeout to 1 second
        LOGGER__TRACE("Reading from the message queue {} from ISP", post_denoise_isp_data);
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
                LOGGER__ERROR("Error receiving post denoise filter data from ISP message: {}", strerror(errno));
                break; // Exit the loop and stop the thread
            }
        }
        m_enabled = m_post_denoise_config.enabled;
        set_dsp_denoise_params_from_isp(m_post_denoise_config);
    }
    mq_close(mq);
}
