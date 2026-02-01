#include <fstream>
#include <nlohmann/json.hpp>
#include "media_library_logger.hpp"
#include "eis.hpp"

#define MODULE_NAME LoggerType::Eis
#define RAD_TO_DEG(x) ((x) * 180.0 / CV_PI)

#define MAX_SKIPPED_GYRO_SAMPLES 3.0 // the maximum amount of gyro samples that can be missing
#define DELTA_TIME_THRESHOLD(sample_rate)                                                                              \
    (MAX_SKIPPED_GYRO_SAMPLES / sample_rate) // maximum allowed time between gyro samples while integrating

static cv::Mat SLERP(const cv::Mat &r1, const cv::Mat &r2, double rotational_smoothing_coefficient)
{
    /* Convert rotation vectors to rotation matrices */
    cv::Mat mat1 = r1;
    cv::Mat mat2 = r2;
    /* Compute the relative rotation matrix */
    cv::Mat relative_rot_mat = mat2 * mat1.t();
    /* Convert the relative rotation matrix to rotation vector */
    cv::Mat relative_rot_vec;
    cv::Rodrigues(relative_rot_mat, relative_rot_vec);
    /* Scale the rotation vector by rotational_smoothing_coefficient */
    cv::Mat scaled_rot_vec = relative_rot_vec * rotational_smoothing_coefficient;
    /* Convert the scaled rotation vector back to a rotation matrix */
    cv::Mat temp_rot;
    cv::Rodrigues(scaled_rot_vec, temp_rot);
    /* Compute the interpolated rotation matrix */
    cv::Mat interpolated_rot = temp_rot * mat1;
    return interpolated_rot;
}

cv::Mat EIS::smooth(const cv::Mat &current_orientation, double rotational_smoothing_coefficient)
{
    cv::Mat smooth_orientation =
        previous_orientations.is_empty() ? current_orientation.clone() : previous_orientations[0].clone();

    for (size_t i = 1; i < previous_orientations.size(); i++)
    {
        smooth_orientation = SLERP(smooth_orientation, previous_orientations[i], rotational_smoothing_coefficient);
    }

    smooth_orientation = SLERP(smooth_orientation, current_orientation, rotational_smoothing_coefficient);
    previous_orientations.push(current_orientation.clone());

    return smooth_orientation;
}

static inline cv::Mat get_curr_gyro_rotation_mat(const unbiased_gyro_sample_t &gyro_sample, uint64_t start_time,
                                                 uint64_t end_time)
{
    /* Convert to nanoseconds */
    double delta_t = ((double)end_time - (double)start_time) * 1e-9;
    if (delta_t <= 0.0)
    {
        /* In the case when a gyro sample is not synchronised disregard it */
        return cv::Mat::eye(3, 3, CV_64F);
    }
    cv::Vec3d gyro_scaled_rot_vec(gyro_sample.vx * delta_t, gyro_sample.vy * delta_t, gyro_sample.vz * delta_t);
    cv::Mat gyro_rot_mat;
    cv::Rodrigues(gyro_scaled_rot_vec, gyro_rot_mat);
    return gyro_rot_mat;
}

static cv::Mat euler_angles_to_rot_mat(const cv::Vec3d &angles)
{
    double roll = angles[0], pitch = angles[1], yaw = angles[2];

    double sin_roll = std::sin(roll), cos_roll = std::cos(roll);
    double sin_pitch = std::sin(pitch), cos_pitch = std::cos(pitch);
    double sin_yaw = std::sin(yaw), cos_yaw = std::cos(yaw);

    cv::Mat R_x = (cv::Mat_<double>(3, 3) << 1, 0, 0, 0, cos_roll, -sin_roll, 0, sin_roll, cos_roll);

    cv::Mat R_y = (cv::Mat_<double>(3, 3) << cos_pitch, 0, sin_pitch, 0, 1, 0, -sin_pitch, 0, cos_pitch);

    cv::Mat R_z = (cv::Mat_<double>(3, 3) << cos_yaw, -sin_yaw, 0, sin_yaw, cos_yaw, 0, 0, 0, 1);

    return R_z * R_y * R_x;
}

shakes_state_t EIS::get_curr_shakes_state()
{
    double std_angle_deg = RAD_TO_DEG(cv::norm(m_rotation_buffer.standard_deviation()));
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Mean: {}", RAD_TO_DEG(cv::norm(m_rotation_buffer.mean())));

    if (std_angle_deg < m_min_angle_deg)
    {
        return shakes_state_t::NOISE;
    }
    else if (std_angle_deg > m_max_angle_deg)
    {
        return shakes_state_t::VIOLENT;
    }
    else
    {
        return shakes_state_t::NORMAL;
    }
}

std::vector<std::pair<uint64_t, cv::Mat>> EIS::get_orientations_based_on_shakes_state(
    std::vector<std::pair<uint64_t, cv::Mat>> current_orientations)
{
    if (current_orientations.empty())
    {
        return {std::make_pair(0, cv::Mat::eye(3, 3, CV_64F))};
    }

    shakes_state_t curr_shakes_state = get_curr_shakes_state();

    if (curr_shakes_state == shakes_state_t::VIOLENT)
    {
        return {std::make_pair(0, cv::Mat::eye(3, 3, CV_64F))};
    }
    else if (curr_shakes_state == shakes_state_t::NOISE)
    {
        /* In Noise state return the last Normal state orientations with the current timestamps */
        for (size_t i = 0; i < current_orientations.size(); ++i)
        {
            current_orientations[i].second = last_normal_shakes_state_orientations;
        }
    }

    last_normal_shakes_state_orientations = current_orientations[0].second.clone();
    return current_orientations;
}

std::vector<std::pair<uint64_t, cv::Mat>> EIS::integrate_rotations_rolling_shutter(
    const std::vector<unbiased_gyro_sample_t> &gyro_samples)
{
    double dt;
    size_t out_rotations_count = 0;
    std::vector<std::pair<uint64_t, cv::Mat>> out_rotations;

    if (gyro_samples.size() == 0)
    {
        return {std::make_pair(0, cv::Mat::eye(3, 3, CV_64F))};
    }

    for (uint64_t i = 0; i < gyro_samples.size(); i++)
    {
        if (m_last_sample.timestamp_ns == 0)
            dt = ((double)gyro_samples[1].timestamp_ns - (double)gyro_samples[0].timestamp_ns) * 1e-9;
        else if (i == 0)
            dt = ((double)gyro_samples[i].timestamp_ns - (double)m_last_sample.timestamp_ns) * 1e-9;
        else
            dt = ((double)gyro_samples[i].timestamp_ns - (double)gyro_samples[i - 1].timestamp_ns) * 1e-9;

        if (dt >
            DELTA_TIME_THRESHOLD(
                m_sample_rate)) // Gap between samples is too big, probably some dropped samples, skip this integration
        {
            LOGGER__MODULE__INFO(
                MODULE_NAME, "integrate_rotations_rolling_shutter time delta is too big: {} skipping integration", dt);
            continue;
        }
        if (dt < 0) // Gap is negative, probably a messed up sample
        {
            LOGGER__MODULE__INFO(MODULE_NAME, "integrate_rotations_rolling_shutter time delta is negative: {}", dt);
            if (i > 0)
            {
                // since this is a messed up sample, remove the last integration it took a part in too.
                LOGGER__MODULE__INFO(MODULE_NAME,
                                     "reverting current angle {} to previous angle {} and popping back {} items",
                                     m_cur_angle, m_prev_angle, out_rotations.size() - out_rotations_count);
                m_cur_angle = m_prev_angle;
                while (out_rotations.size() > out_rotations_count)
                {
                    out_rotations.pop_back();
                }
            }
            continue;
        }

        m_prev_angle = m_cur_angle;
        m_cur_angle += cv::Vec3d(gyro_samples[i].vx, gyro_samples[i].vy, gyro_samples[i].vz) * dt;
        m_rotation_buffer.push(m_cur_angle);
        cv::Mat delta_rot = euler_angles_to_rot_mat(m_cur_angle);
        cv::Mat rot_camera = (m_gyro_to_cam_rot_mat * delta_rot.t()) * m_gyro_to_cam_rot_mat.t();
        out_rotations.emplace_back(std::pair<uint64_t, cv::Mat>(gyro_samples[i].timestamp_ns, rot_camera));
        out_rotations_count = out_rotations.size();
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Integrated {} gyro samples, current angles: {}, samples std: {}",
                          out_rotations.size(), m_cur_angle,
                          RAD_TO_DEG(cv::norm(m_rotation_buffer.standard_deviation())));

    m_last_sample = gyro_samples.back();

    return out_rotations;
}

void EIS::remove_bias(const std::vector<gyro_sample_t> &gyro_records,
                      std::vector<unbiased_gyro_sample_t> &unbiased_records)
{
    unbiased_records.clear();
    unbiased_records.reserve(gyro_records.size());

    for (const auto &gyro : gyro_records)
    {
        unbiased_records.emplace_back(unbiased_gyro_sample_t(m_hpf_filters[0].filter(gyro.vx),
                                                             m_hpf_filters[1].filter(gyro.vy),
                                                             m_hpf_filters[2].filter(gyro.vz), gyro.timestamp_ns));
    }

    m_hpf_filters[0].on_frame_end();
    m_hpf_filters[1].on_frame_end();
    m_hpf_filters[2].on_frame_end();
}

bool EIS::converged()
{
    return m_hpf_filters[0].converged() && m_hpf_filters[1].converged() && m_hpf_filters[2].converged();
}

static cv::Mat get_rotation_by_timestamp(uint64_t query_timestamp,
                                         const std::vector<std::pair<uint64_t, cv::Mat>> &rotations_buffer)
{
    if (rotations_buffer.empty() || rotations_buffer[0].first == 0)
    {
        return cv::Mat::eye(3, 3, CV_32F);
    }

    /* First element with a timestamp greater than query_timestamp */
    auto upper_it = std::upper_bound(
        rotations_buffer.begin(), rotations_buffer.end(), query_timestamp,
        [](uint64_t value, const std::pair<uint64_t, cv::Mat> &element) { return value <= element.first; });

    if (upper_it == rotations_buffer.begin())
    {
        return upper_it->second;
    }

    /* Get the closest lower-bound rotation */
    auto lower_it = std::prev(upper_it);
    if (upper_it == rotations_buffer.end())
    {
        return lower_it->second;
    }
    /* Calculate the interpolation between the two */
    uint64_t delta = query_timestamp - lower_it->first;
    uint64_t gyro_rate = upper_it->first - lower_it->first;
    double tau = static_cast<double>(delta) / static_cast<double>(gyro_rate);

    const cv::Mat &r1 = lower_it->second;
    const cv::Mat &r2 = upper_it->second;
    cv::Mat closest_rotation = SLERP(r1, r2, tau);

    return closest_rotation;
}

std::vector<cv::Mat> EIS::get_rolling_shutter_rotations(
    const std::vector<std::pair<uint64_t, cv::Mat>> &rotations_buffer, int grid_height,
    uint64_t middle_exposure_time_of_first_row, std::vector<uint64_t> frame_readout_times, float camera_fov_factor)
{
    uint64_t frame_readout_time = frame_readout_times[0];
    std::vector<cv::Mat> out_rotations(grid_height);
    for (int y = 0; y < grid_height; y++)
    {
        cv::Mat stab_rot = cv::Mat::eye(3, 3, CV_32F);
        if (middle_exposure_time_of_first_row != 0)
        {
            /* Instead of using the raw grid row index `y`, we compute a weighted row position `Y`
               that blends between the actual row index and the image center. This adjustment accounts
               for the camera field-of-view factor (`camera_fov_factor`):
                - When camera_fov_factor = 1.0 → Y ≈ y (no adjustment).
                - When camera_fov_factor < 1.0 → rows are biased toward the image center,
                    modeling reduced sensitivity at the edges of the frame.
                This makes the rolling-shutter row timing estimation more accurate. */
            int Y = static_cast<int>((camera_fov_factor * y) + ((1.0f - camera_fov_factor) * (grid_height / 2.0f)));
            double row_fraction = static_cast<double>(Y) / static_cast<double>(grid_height);
            row_fraction = std::min(row_fraction, 1.0);

            uint64_t row_time =
                middle_exposure_time_of_first_row + static_cast<uint64_t>(row_fraction * frame_readout_time);
            stab_rot = get_rotation_by_timestamp(row_time, rotations_buffer);
        }
        stab_rot.convertTo(out_rotations[y], CV_32F);
    }

    return out_rotations;
}

static int parse_gyro_calibration_config_file(const std::string &filename,
                                              gyro_calibration_config_t &gyro_calibration_config)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "parse_gyro_calibration_config_file could not open file {}", filename);
        return -1;
    }

    nlohmann::json jsonData;
    file >> jsonData;

    std::map<std::string, float gyro_calibration_config_t::*> floatFields = {
        {"gbias_x", &gyro_calibration_config_t::gbias_x}, {"gbias_y", &gyro_calibration_config_t::gbias_y},
        {"gbias_z", &gyro_calibration_config_t::gbias_z}, {"rot_x", &gyro_calibration_config_t::rot_x},
        {"rot_y", &gyro_calibration_config_t::rot_y},     {"rot_z", &gyro_calibration_config_t::rot_z},
    };

    for (const auto &[field, member] : floatFields)
    {
        if (jsonData.find(field) == jsonData.end())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "parse_gyro_calibration_config_file could not find field {} in {}",
                                  field, filename);
            return -1;
        }
        gyro_calibration_config.*member = jsonData.at(field).get<float>();
    }

    return 0;
}

EIS::~EIS() = default;

EIS::EIS(const std::string &config_filename, uint32_t window_size, uint32_t sample_rate, float min_angle_degrees,
         float max_angle_degrees, size_t shakes_type_buff_size, double iir_hpf_coefficient, double gyro_scale)
    : m_sample_rate(sample_rate), m_rotation_buffer(shakes_type_buff_size)
{
    if (parse_gyro_calibration_config_file(config_filename, m_gyro_calibration_config) == -1)
    {
        LOGGER__MODULE__ERROR(
            MODULE_NAME,
            "EIS: Failed to parse gyro calibration config file, configuring all calibration values with 0's");
        m_gyro_calibration_config = {0, 0, 0, 0, 0, 0};
    }

    previous_orientations.set_capacity(window_size);

    /* Create a 3x3 identity matrix as the first matrix in the "previous orientations" */
    previous_orientations.push(cv::Mat::eye(3, 3, CV_64F));
    m_cur_angle = cv::Vec3d(0.0, 0.0, 0.0);
    m_last_sample = unbiased_gyro_sample_t(0, 0, 0, 0);
    cv::Vec3d calibs_rot_vec(m_gyro_calibration_config.rot_x, m_gyro_calibration_config.rot_y,
                             m_gyro_calibration_config.rot_z);
    cv::Rodrigues(calibs_rot_vec, m_gyro_to_cam_rot_mat);
    m_frame_count = 0;
    m_min_angle_deg = min_angle_degrees;
    m_max_angle_deg = max_angle_degrees;
    last_normal_shakes_state_orientations = cv::Mat::eye(3, 3, CV_64F);

    m_hpf_filters.emplace_back(iir_hpf_coefficient, gyro_scale, m_gyro_calibration_config.gbias_x); // X-axis filter
    m_hpf_filters.emplace_back(iir_hpf_coefficient, gyro_scale, m_gyro_calibration_config.gbias_y); // Y-axis filter
    m_hpf_filters.emplace_back(iir_hpf_coefficient, gyro_scale, m_gyro_calibration_config.gbias_z); // Z-axis filter
}

bool EIS::check_periodic_reset(std::vector<cv::Mat> &rolling_shutter_rotations, uint32_t curr_fps)
{
    /* If we haven't yet reached the hard deadline, check if this is a "good" time for reset:
        If all of the rotation matrices are close to the identity matrix, that way the reset will
        have less of a visual impact. Meaning:

        if frame_count in [EIS_RESET_FRAMES_NUM ,EIS_RESET_FRAMES_NUM + EIS_OPTIMAL_RESET_FRAMES_CHECK_NUM]:
            reset EIS only if all the rotation matrices are close to the identity matrix (all the angels are less
       then the threshold). if frame_count >= EIS_RESET_FRAMES_NUM + EIS_OPTIMAL_RESET_FRAMES_CHECK_NUM reset EIS
    */
    if (m_frame_count < ((curr_fps * EIS_RESET_TIME) + EIS_OPTIMAL_RESET_FRAMES_CHECK_NUM))
    {
        for (auto &rotation : rolling_shutter_rotations)
        {
            cv::Mat rvec;
            cv::Rodrigues(rotation, rvec);
            double angle = cv::norm(rvec);

            if (angle > EIS_RESET_ANGLES_THRESHOLD)
            {
                /* If one of the angles is above the threshold,
                    we are not close enough to the identity matrix */
                return false;
            }
        }
    }

    return true;
}

void EIS::reset_history(bool reset_hpf)
{
    LOGGER__MODULE__WARNING(MODULE_NAME, "[EIS] EIS reset!");
    previous_orientations.clear();
    previous_orientations.push(cv::Mat::eye(3, 3, CV_64F));
    m_cur_angle = cv::Vec3d(0.0, 0.0, 0.0);
    m_last_sample = unbiased_gyro_sample_t(0, 0, 0, 0);
    m_frame_count = 0;
    last_normal_shakes_state_orientations = cv::Mat::eye(3, 3, CV_64F);
    m_rotation_buffer.clear();

    if (reset_hpf)
    {
        m_hpf_filters[0].reset();
        m_hpf_filters[1].reset();
        m_hpf_filters[2].reset();
    }
}

uint64_t EIS::get_middle_exposure_timestamp(uint64_t last_xvs_timestamp,
                                            isp_utils::isp_hdr_sensor_params_t &hdr_sensor_params, float t,
                                            uint64_t &threshold_timestamp)
{
    uint8_t num_exposures = hdr_sensor_params.shr_times.size();
    uint64_t shr0 = hdr_sensor_params.shr_times[0];
    uint64_t vmax = hdr_sensor_params.vmax;
    uint64_t readout_time = hdr_sensor_params.rhs_times[0]; // NUM_READOUT_LINES_4K * line_readout_time
    uint64_t middle_exposure_first_line = 0;

    if (num_exposures == 1)
    {
        /* SDR */
        uint64_t integration_time_sdr = vmax - shr0;
        middle_exposure_first_line = last_xvs_timestamp - (integration_time_sdr / 2);
        uint64_t middle_exposure_last_line = middle_exposure_first_line + readout_time;
        threshold_timestamp = middle_exposure_last_line;
    }
    else if (num_exposures == 2)
    {
        /* 2DOL */
        uint64_t shr1 = hdr_sensor_params.shr_times[1];
        uint64_t rhs1 = hdr_sensor_params.rhs_times[1];
        uint64_t integration_time_lef = 2 * vmax - shr0;
        uint64_t integration_time_sef = rhs1 - shr1;
        uint64_t middle_exposure_first_line_lef = last_xvs_timestamp - (integration_time_lef / 2);
        uint64_t middle_exposure_first_line_sef = last_xvs_timestamp + shr1 + (integration_time_sef / 2);
        middle_exposure_first_line = (t * middle_exposure_first_line_lef) + ((1 - t) * middle_exposure_first_line_sef);
    }

    return middle_exposure_first_line;
}
