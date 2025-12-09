#include "dsp_image_enhancement.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <tuple>
#include "media_library_logger.hpp"

#define MODULE_NAME LoggerType::Dsp

DspImageEnhancement::DspImageEnhancement()
    : m_denoise_element_enabled(false), m_enabled(false), m_running(true),
      m_isp_params{
          .enabled = false,
          .auto_luma = false,
          .manual_contrast = 1.0,
          .manual_brightness = 0,
          .auto_percentile_low = 2.0,
          .auto_percentile_high = 99.9,
          .auto_target_low = 5,
          .auto_target_high = 248,
          .auto_low_pass_filter_alpha = 0.95,
          .bilateral_denoise = false,
          .blur_level = 0,
          .bilateral_sigma = 30,
          .sharpness_level = 0,
          .sharpness_amount = 0,
          .sharpness_threshold = 0,
          .saturation = 1.0,
          .histogram_equalization = false,
          .histogram_equalization_alpha = 0.5,
          .histogram_equalization_clip_threshold = 1.0,
      },
      m_dsp_histogram_params{
          .x_sample_step = 29,
          .y_sample_step = 29,
          .histogram = {0},
      },
      m_histogram_eq_params{}, m_dsp_params(get_default_disabled_dsp_params()), m_histogram_clip_thr(1.0),
      m_histogram_alpha(0.5), m_isp_params_update_thread(&DspImageEnhancement::read_params_from_isp, this),
      m_brightness(std::nullopt)
{
}

DspImageEnhancement::~DspImageEnhancement()
{
    m_running = false;
    if (m_isp_params_update_thread.joinable())
    {
        m_isp_params_update_thread.join();
    }
}

bool DspImageEnhancement::is_enabled()
{
    return m_enabled;
}

dsp_image_enhancement_params_t DspImageEnhancement::get_default_disabled_dsp_params()
{
    dsp_image_enhancement_params_t params{
        .blur =
            {
                .level = 0,
            },
        .bilateral =
            {
                .enabled = false,
                .sigma_color = 0,
            },
        .sharpness =
            {
                .level = 0,
                .amount = 0,
                .threshold = 0,
            },
        .color =
            {
                .contrast = 1.0,
                .brightness = 0,
                .saturation_u_a = 1.0,
                .saturation_u_b = 0,
                .saturation_v_a = 1.0,
                .saturation_v_b = 0,
            },
        .histogram_params = m_do_histogram_equalization ? &m_dsp_histogram_params : nullptr,
        .histogram_equalization_params = m_do_histogram_equalization ? &m_histogram_eq_params : nullptr,
    };

    return params;
}

dsp_image_enhancement_params_t DspImageEnhancement::get_dsp_params()
{
    std::shared_lock<std::shared_mutex> lock(m_dsp_params_lock);
    return m_dsp_params;
}

std::pair<uint16_t, uint16_t> DspImageEnhancement::histogram_sample_step_for_frame(std::pair<size_t, size_t> frame_size,
                                                                                   uint32_t sample_size)
{
    auto [width, height] = frame_size;
    float aspect_ratio = width / (float)height;
    int n_height = std::sqrt(sample_size / aspect_ratio);
    int n_width = sample_size / n_height;
    int delta_width = width / n_width + 1;
    int delta_height = height / n_height + 1;

    return std::make_pair(delta_width, delta_height);
}

std::pair<uint8_t, uint8_t> DspImageEnhancement::find_percentile_pixels(const Histogram &histogram,
                                                                        float percentile_low, float percentile_high)
{
    // Calculate the total number of pixels
    uint32_t total_pixels = std::accumulate(std::begin(histogram), std::end(histogram), 0);

    // Calculate the target counts for low and high percentiles
    uint32_t target_low_percent = static_cast<uint32_t>(total_pixels * percentile_low / 100.0);
    uint32_t target_high_percent = static_cast<uint32_t>(total_pixels * percentile_high / 100.0);

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

void DspImageEnhancement::contrast_brightness_lowpass_filter(float contrast, int16_t brightness, float &new_contrast,
                                                             float &new_brightness)
{
    float previous_contrast = m_dsp_params.color.contrast;
    float previous_brightness = m_brightness.value();
    new_contrast = m_isp_params.auto_low_pass_filter_alpha * previous_contrast +
                   (1 - m_isp_params.auto_low_pass_filter_alpha) * contrast;
    new_brightness = m_isp_params.auto_low_pass_filter_alpha * previous_brightness +
                     (1 - m_isp_params.auto_low_pass_filter_alpha) * brightness;
}

std::pair<float, int16_t> DspImageEnhancement::contrast_brightness_from_percentiles(uint8_t low_percentile_pixel,
                                                                                    uint8_t high_percentile_pixel)
{
    float b = m_isp_params.auto_target_low;
    float a = static_cast<float>(m_isp_params.auto_target_high - b) /
              (static_cast<float>(high_percentile_pixel - low_percentile_pixel) + 1e-6f);

    float contrast = a;
    int16_t brightness = b - a * low_percentile_pixel;

    return std::make_pair(contrast, brightness);
}

std::vector<double> DspImageEnhancement::clip_histogram(const Histogram &histogram, double clip_threshold)
{
    std::vector<double> clipped_hist(DSP_HISTOGRAM_SIZE);
    std::transform(std::begin(histogram), std::end(histogram), clipped_hist.begin(),
                   [](auto h) { return static_cast<double>(h); });

    double sum_pixels_in_hist = std::accumulate(clipped_hist.begin(), clipped_hist.end(), 0.0);
    double actual_clip_limit = clip_threshold * sum_pixels_in_hist / DSP_HISTOGRAM_SIZE;

    double excess = 0;
    for (auto &curr : clipped_hist)
    {
        if (curr > actual_clip_limit)
        {
            excess += curr - actual_clip_limit;
            curr = actual_clip_limit;
        }
    }

    /* Redistribute the excess between all the indeces in the histogram */
    double redist = excess / DSP_HISTOGRAM_SIZE;
    for (auto &curr : clipped_hist)
    {
        curr += redist;
    }

    return clipped_hist;
}

void DspImageEnhancement::update_lut(const Histogram &histogram)
{
    auto clipped_hist = clip_histogram(histogram, m_histogram_clip_thr);

    /* Compute CDF */
    std::vector<double> cdf(DSP_HISTOGRAM_SIZE);
    /* cdf[i] = cdf[i - 1] + clipped_hist[i] */
    std::partial_sum(clipped_hist.begin(), clipped_hist.end(), cdf.begin());

    /* Normalize CDF and create LUT */
    double cdf_max = cdf.back();
    for (size_t i = 0; i < DSP_HISTOGRAM_SIZE; i++)
    {
        m_histogram_eq_params.lut[i] =
            static_cast<uint8_t>(m_histogram_alpha * m_histogram_eq_params.lut[i] +
                                 (1 - m_histogram_alpha) * (((cdf[i] * 255.0) / cdf_max) + 0.5));
    }
}

void DspImageEnhancement::update_dsp_params_from_histogram(bool is_denoise_enabled, const Histogram &histogram)
{
    if (m_do_histogram_equalization)
    {
        update_lut(histogram);
    }

    if (!is_denoise_enabled)
    {
        return;
    }

    auto [low_percentile_pixel, high_percentile_pixel] =
        find_percentile_pixels(histogram, m_isp_params.auto_percentile_low, m_isp_params.auto_percentile_high);
    auto [contrast, brightness] = contrast_brightness_from_percentiles(low_percentile_pixel, high_percentile_pixel);
    contrast = std::clamp(contrast, 0.0f, 10.0f);
    brightness = std::clamp(static_cast<int>(brightness), -128, 128);

    std::shared_lock<std::shared_mutex> lock(m_dsp_params_lock);
    // if histogram equalization is enabled, or if we are in manual mode, we don't need to update the
    // contrast and brightness parameters from the histogram
    if (!m_dsp_params.histogram_params || m_do_histogram_equalization)
    {
        return;
    }

    float new_contrast, new_brightness;
    if (m_brightness)
    {
        // apply lowpass filter only if we've already sampled a histogram once
        contrast_brightness_lowpass_filter(contrast, brightness, new_contrast, new_brightness);
        LOGGER__MODULE__TRACE(MODULE_NAME,
                              "image enhancement parameters calcualted from the histogram: "
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
        LOGGER__MODULE__TRACE(MODULE_NAME,
                              "image enhancement parameters calcualted from the histogram: "
                              "low percentile pixel {} high percentile pixel {} "
                              "contrast: {} brightness: {} (clipping without low-pass filter)",
                              low_percentile_pixel, high_percentile_pixel, contrast, brightness);
    }

    m_dsp_params.color.contrast = new_contrast;
    m_dsp_params.color.brightness = new_brightness;
    m_brightness = new_brightness;
}

void DspImageEnhancement::update_dsp_params_from_isp()
{
    float saturation_a = m_isp_params.saturation;
    int16_t saturation_b = static_cast<int16_t>(128 * (1 - m_isp_params.saturation));

    std::unique_lock<std::shared_mutex> lock(m_dsp_params_lock);
    if (m_isp_params.bilateral_denoise)
    {
        m_dsp_params.bilateral.enabled = m_isp_params.bilateral_denoise;
        m_dsp_params.bilateral.sigma_color = m_isp_params.bilateral_sigma;
        m_dsp_params.blur.level = 0;
    }
    else
    {
        m_dsp_params.bilateral.enabled = false;
        m_dsp_params.blur.level = m_isp_params.blur_level;
    }
    m_dsp_params.sharpness.level = m_isp_params.sharpness_level;
    m_dsp_params.sharpness.amount = m_isp_params.sharpness_amount;
    m_dsp_params.sharpness.threshold = m_isp_params.sharpness_threshold;
    m_dsp_params.color.saturation_u_a = saturation_a;
    m_dsp_params.color.saturation_u_b = saturation_b;
    m_dsp_params.color.saturation_v_a = saturation_a;
    m_dsp_params.color.saturation_v_b = saturation_b;
    m_do_histogram_equalization = m_isp_params.histogram_equalization;
    m_histogram_clip_thr = m_isp_params.histogram_equalization_clip_threshold;
    m_histogram_alpha = m_isp_params.histogram_equalization_alpha;

    if (m_isp_params.histogram_equalization)
    {
        m_dsp_params.histogram_params = &m_dsp_histogram_params;
        m_dsp_params.histogram_equalization_params = &m_histogram_eq_params;
        m_dsp_params.color.contrast = 1;   // "Disable" contrast for histogram equalization
        m_dsp_params.color.brightness = 0; // "Disable" brightness for histogram equalization
        m_brightness = std::nullopt;
    }
    else if (m_isp_params.auto_luma)
    {
        m_dsp_params.histogram_params = &m_dsp_histogram_params;
        m_dsp_params.histogram_equalization_params = nullptr;
    }
    else
    {
        m_dsp_params.color.contrast = m_isp_params.manual_contrast;
        m_dsp_params.color.brightness = m_isp_params.manual_brightness;
        m_dsp_params.histogram_params = nullptr;
        m_brightness = std::nullopt;
        m_dsp_params.histogram_equalization_params = nullptr;
    }
}

void DspImageEnhancement::read_params_from_isp()
{
    struct mq_attr attr;
    attr.mq_flags = O_NONBLOCK;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(m_isp_params);
    attr.mq_curmsgs = 0;

    mqd_t mq = mq_open(isp_data, O_RDONLY | O_CREAT | O_NONBLOCK, 0666, &attr);
    if (mq == (mqd_t)-1)
    {
        LOGGER__MODULE__ERROR(
            MODULE_NAME,
            "Error opening message queue named: {}. with the ISP when post denoise filter is enable for reading",
            isp_data);
        return;
    }
    while (m_running)
    {
        LOGGER__MODULE__TRACE(MODULE_NAME, "Reading from the message queue {} from ISP", isp_data);

        // Non-blocking receive, because mq_timedreceive uses CLOCK_REALTIME which is not immune to system time changes
        ssize_t bytes_read = mq_receive(mq, reinterpret_cast<char *>(&m_isp_params), sizeof(m_isp_params), NULL);

        if (bytes_read < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                LOGGER__MODULE__TRACE(MODULE_NAME, "No message available, waiting 1 second");

                // Instead of timeout in the receive function, wait 1 second between receive calls
                struct timespec sleep_time;
                sleep_time.tv_sec = 1;
                sleep_time.tv_nsec = 0;
                // Use CLOCK_MONOTONIC to be immune to system time changes
                clock_nanosleep(CLOCK_MONOTONIC, 0, &sleep_time, NULL);
                continue;
            }
            else
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Error receiving post denoise filter data from ISP message: {}",
                                      strerror(errno));
                break; // Exit the loop and stop the thread
            }
        }
        m_enabled = m_isp_params.enabled;
        // NOTE: static_cast is required here because packed struct fields cannot be passed by reference
        // to variadic functions (such as the logger macro) due to alignment restrictions. Casting to the
        // appropriate value type ensures safe passing by value.
        LOGGER__MODULE__TRACE(
            MODULE_NAME,
            "Received post denoise filter data from ISP:\n"
            "  enabled: {}\n"
            "  auto_luma: {}\n"
            "  manual_contrast: {}\n"
            "  manual_brightness: {}\n"
            "  auto_percentile_low: {}\n"
            "  auto_percentile_high: {}\n"
            "  auto_target_low: {}\n"
            "  auto_target_high: {}\n"
            "  auto_low_pass_filter_alpha: {}\n"
            "  bilateral_denoise: {}\n"
            "  blur_level: {}\n"
            "  bilateral_sigma: {}\n"
            "  sharpness_level: {}\n"
            "  sharpness_amount: {}\n"
            "  sharpness_threshold: {}\n"
            "  saturation: {}\n"
            "  histogram_equalization: {}\n"
            "  histogram_equalization_alpha: {}\n"
            "  histogram_equalization_clip_threshold: {}",
            m_isp_params.enabled, m_isp_params.auto_luma, static_cast<float>(m_isp_params.manual_contrast),
            static_cast<int16_t>(m_isp_params.manual_brightness), static_cast<float>(m_isp_params.auto_percentile_low),
            static_cast<float>(m_isp_params.auto_percentile_high), static_cast<uint8_t>(m_isp_params.auto_target_low),
            static_cast<uint8_t>(m_isp_params.auto_target_high),
            static_cast<float>(m_isp_params.auto_low_pass_filter_alpha), m_isp_params.bilateral_denoise,
            static_cast<uint8_t>(m_isp_params.blur_level), static_cast<uint8_t>(m_isp_params.bilateral_sigma),
            static_cast<uint8_t>(m_isp_params.sharpness_level), static_cast<float>(m_isp_params.sharpness_amount),
            static_cast<uint8_t>(m_isp_params.sharpness_threshold), static_cast<float>(m_isp_params.saturation),
            m_isp_params.histogram_equalization, static_cast<float>(m_isp_params.histogram_equalization_alpha),
            static_cast<float>(m_isp_params.histogram_equalization_clip_threshold));
        update_dsp_params_from_isp();
    }
    mq_close(mq);
}

double DspImageEnhancement::get_histogram_clip_thr()
{
    return m_histogram_clip_thr;
}

void DspImageEnhancement::set_histogram_clip_thr(double clip_thr)
{
    m_histogram_clip_thr = clip_thr;
}

double DspImageEnhancement::get_histogram_alpha()
{
    return m_histogram_alpha;
}

void DspImageEnhancement::set_histogram_alpha(double alpha)
{
    m_histogram_alpha = alpha;
}

bool DspImageEnhancement::is_histogram_equalization_enabled()
{
    return m_do_histogram_equalization;
}

void DspImageEnhancement::set_histogram_equalization_enabled(bool enabled)
{
    m_do_histogram_equalization = enabled;
}

const dsp_histogram_equalization_params_t *DspImageEnhancement::get_histogram_eq_params() const
{
    return &m_histogram_eq_params;
}
