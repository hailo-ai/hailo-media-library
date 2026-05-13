#pragma once
#include <vector>
#include <stdexcept>
#include <opencv2/opencv.hpp>
#include "media_library/eis_types.hpp"
#include "media_library/isp_utils.hpp"
#include "iir_filter.hpp"
#include "eis_utils.hpp"

/* The time after which we want to reset the EIS (10 minutes: 60 seconds * 10) */
#define EIS_RESET_TIME (60 * 10)

/* The delta after EIS_RESET_FRAMES_NUM after which we reset no matter what */
#define EIS_OPTIMAL_RESET_FRAMES_CHECK_NUM (600)

/* The threshold we consider to be "close enough" to the identity
    matrix, is used when periodically resetting EIS */
#define EIS_RESET_ANGLES_THRESHOLD (0.1 * (CV_PI / 180.0))

struct gyro_calibration_config_t
{
    float gbias_x;
    float gbias_y;
    float gbias_z;
    float rot_x;
    float rot_y;
    float rot_z;
};

enum class shakes_state_t
{
    NORMAL,
    NOISE,
    VIOLENT,
};

class EIS
{
  public:
    size_t m_frame_count;

    EIS(const std::string &config_filename, uint32_t window_size, uint32_t sample_rate, float min_angle_degrees,
        float max_angle_degrees, size_t shakes_type_buff_size, double iir_hpf_coefficient, double gyro_scale);
    ~EIS();

    cv::Mat smooth(const cv::Mat &current_orientation, double rotational_smoothing_coefficient);
    std::vector<std::pair<uint64_t, cv::Mat>> integrate_rotations_rolling_shutter(
        const std::vector<unbiased_gyro_sample_t> &frame_gyro_records);
    void remove_bias(const std::vector<gyro_sample_t> &gyro_records,
                     std::vector<unbiased_gyro_sample_t> &unbiased_records);
    bool converged();
    std::vector<cv::Mat> get_rolling_shutter_rotations(
        const std::vector<std::pair<uint64_t, cv::Mat>> &rotations_buffer, int grid_height,
        uint64_t middle_exposure_time_of_first_row, std::vector<uint64_t> frame_readout_times, float camera_fov_factor);

    bool check_periodic_reset(std::vector<cv::Mat> &rolling_shutter_rotations, uint32_t curr_fps);
    void reset_history(bool reset_hpf);
    shakes_state_t get_curr_shakes_state();
    std::vector<std::pair<uint64_t, cv::Mat>> get_orientations_based_on_shakes_state(
        std::vector<std::pair<uint64_t, cv::Mat>> current_orientations);

    /*
     * Calculate the timestamp of the middle exposure line
     * according to the sensor parameters and last XVS.
     */
    uint64_t get_middle_exposure_timestamp(uint64_t timestamp, isp_utils::isp_hdr_sensor_params_t &hdr_sensor_params,
                                           float t, uint64_t &threshold_timestamp);

  private:
    uint32_t m_sample_rate;
    gyro_calibration_config_t m_gyro_calibration_config;
    eis_utils::CircularBuffer<cv::Mat> previous_orientations;
    cv::Mat m_gyro_to_cam_rot_mat;
    unbiased_gyro_sample_t m_last_sample = unbiased_gyro_sample_t(0, 0, 0, 0);
    cv::Vec3d m_cur_angle = cv::Vec3d(0.0, 0.0, 0.0);
    cv::Vec3d m_prev_angle = cv::Vec3d(0.0, 0.0, 0.0);
    uint64_t m_latest_time = 0;
    eis_utils::Vec3dFifoBuffer m_rotation_buffer;
    float m_min_angle_deg = 0.0f;
    float m_max_angle_deg = 180.0f;
    cv::Mat last_normal_shakes_state_orientations;
    std::vector<IIRFilter> m_hpf_filters;
};
