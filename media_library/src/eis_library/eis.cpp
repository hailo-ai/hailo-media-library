#include <fstream>
#include <nlohmann/json.hpp>
#include "media_library_logger.hpp"
#include "eis.hpp"

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

cv::Mat EIS::integrate_rotations(uint64_t last_threshold_timestamp, uint64_t curr_threshold_timestamp,
                                 const std::vector<unbiased_gyro_sample_t> &frame_gyro_records)
{
    if (frame_gyro_records.size() == 0)
    {
        return m_prev_total_rotation.clone();
    }

    cv::Mat gyro_adjusted_orientation =
        m_prev_total_rotation *
        get_curr_gyro_rotation_mat(frame_gyro_records[0], last_threshold_timestamp, frame_gyro_records[0].timestamp_ns);
    for (size_t i = 1; i < frame_gyro_records.size(); i++)
    {
        gyro_adjusted_orientation *= get_curr_gyro_rotation_mat(
            frame_gyro_records[i], frame_gyro_records[i - 1].timestamp_ns, frame_gyro_records[i].timestamp_ns);
    }

    m_prev_total_rotation *= get_curr_gyro_rotation_mat(frame_gyro_records[frame_gyro_records.size() - 1],
                                                        frame_gyro_records[frame_gyro_records.size() - 1].timestamp_ns,
                                                        curr_threshold_timestamp);

    m_prev_total_rotation = gyro_adjusted_orientation.clone();
    gyro_adjusted_orientation = (m_gyro_to_cam_rot_mat * gyro_adjusted_orientation) * m_gyro_to_cam_rot_mat.t();

    return gyro_adjusted_orientation;
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

std::vector<std::pair<uint64_t, cv::Mat>> EIS::integrate_rotations_rolling_shutter(
    const std::vector<unbiased_gyro_sample_t> &gyro_samples)
{
    double dt;
    std::vector<std::pair<uint64_t, cv::Mat>> out_rotations;

    if (gyro_samples.size() == 0)
    {
        return {std::make_pair(0, m_prev_total_rotation.clone())};
    }

    for (uint64_t i = 0; i < gyro_samples.size(); i++)
    {
        if (m_last_sample.timestamp_ns == 0)
            dt = ((double)gyro_samples[1].timestamp_ns - (double)gyro_samples[0].timestamp_ns) * 1e-9;
        else if (i == 0)
            dt = ((double)gyro_samples[i].timestamp_ns - (double)m_last_sample.timestamp_ns) * 1e-9;
        else
            dt = ((double)gyro_samples[i].timestamp_ns - (double)gyro_samples[i - 1].timestamp_ns) * 1e-9;

        if (dt < 0)
            continue;

        m_cur_angle += cv::Vec3d(gyro_samples[i].vx, gyro_samples[i].vy, gyro_samples[i].vz) * dt;
        cv::Mat delta_rot = euler_angles_to_rot_mat(m_cur_angle);
        cv::Mat rot_camera = (m_gyro_to_cam_rot_mat * delta_rot.t()) * m_gyro_to_cam_rot_mat.t();
        out_rotations.emplace_back(std::pair<uint64_t, cv::Mat>(gyro_samples[i].timestamp_ns, rot_camera));
    }

    m_last_sample = gyro_samples.back();

    return out_rotations;
}

void EIS::remove_bias(const std::vector<gyro_sample_t> &gyro_records,
                      std::vector<unbiased_gyro_sample_t> &unbiased_records, double gyro_scale,
                      double iir_hpf_coefficient)
{
    /* Precompute invariant part of the filter formula
       y_n = iir_hpf_coefficient * y_(n-1) + (1 + iir_hpf_coefficient) / 2 * (x_n - x_(n-1))
       y_curr = iir_hpf_coefficient * y_prev + ((1 + iir_hpf_coefficient) / 2) * (x_curr - x_prev) */
    double one_plus_beta_over_two = (1 + iir_hpf_coefficient) / 2;

    unbiased_records.clear();
    unbiased_records.reserve(gyro_records.size());

    auto apply_high_pass_filter = [&](double current_value, double &prev_value, double &prev_smooth) -> double {
        double filtered_value =
            iir_hpf_coefficient * prev_smooth + one_plus_beta_over_two * (current_value - prev_value);
        prev_value = current_value;
        prev_smooth = filtered_value;
        return filtered_value;
    };

    for (const auto &gyro : gyro_records)
    {
        double corrected_vx =
            static_cast<double>(gyro.vx) * gyro_scale - static_cast<double>(m_gyro_calibration_config.gbias_x);
        double corrected_vy =
            static_cast<double>(gyro.vy) * gyro_scale - static_cast<double>(m_gyro_calibration_config.gbias_y);
        double corrected_vz =
            static_cast<double>(gyro.vz) * gyro_scale - static_cast<double>(m_gyro_calibration_config.gbias_z);

        double smooth_x =
            apply_high_pass_filter(corrected_vx, prev_high_pass.prev_gyro_x, prev_high_pass.prev_smooth_x);
        double smooth_y =
            apply_high_pass_filter(corrected_vy, prev_high_pass.prev_gyro_y, prev_high_pass.prev_smooth_y);
        double smooth_z =
            apply_high_pass_filter(corrected_vz, prev_high_pass.prev_gyro_z, prev_high_pass.prev_smooth_z);

        unbiased_records.emplace_back(smooth_x, smooth_y, smooth_z, gyro.timestamp_ns);
    }
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
    uint64_t middle_exposure_time_of_first_row, uint64_t frame_readout_time)
{
    std::vector<cv::Mat> out_rotations(grid_height);
    for (int y = 0; y < grid_height; y++)
    {
        cv::Mat stab_rot = cv::Mat::eye(3, 3, CV_32F);
        if (middle_exposure_time_of_first_row != 0)
        {
            uint64_t row_time =
                middle_exposure_time_of_first_row +
                (uint64_t)(((static_cast<double>(y) + 0.5) / static_cast<double>(grid_height)) * frame_readout_time);
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
        LOGGER__ERROR("parse_gyro_calibration_config_file could not open file {}", filename);
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
            LOGGER__ERROR("parse_gyro_calibration_config_file could not find field {} in {}", field, filename);
            return -1;
        }
        gyro_calibration_config.*member = jsonData.at(field).get<float>();
    }

    return 0;
}

EIS::EIS(const std::string &config_filename, uint32_t window_size)
{
    std::cout << "[EIS] EIS enabled!" << std::endl;
    if (parse_gyro_calibration_config_file(config_filename, m_gyro_calibration_config) == -1)
    {
        LOGGER__ERROR("EIS: Failed to parse gyro calibration config file, configuring all calibration values with 0's");
        m_gyro_calibration_config = {0, 0, 0, 0, 0, 0};
    }

    previous_orientations.set_capacity(window_size);

    /* Create a 3x3 identity matrix as the first matrix in the "previous orientations" */
    previous_orientations.push(cv::Mat::eye(3, 3, CV_64F));
    prev_high_pass = {0, 0, 0, 0, 0, 0};
    m_prev_total_rotation = cv::Mat::eye(3, 3, CV_64F);
    m_cur_angle = cv::Vec3d(0.0, 0.0, 0.0);
    m_last_sample = unbiased_gyro_sample_t(0, 0, 0, 0);
    cv::Vec3d calibs_rot_vec(m_gyro_calibration_config.rot_x, m_gyro_calibration_config.rot_y,
                             m_gyro_calibration_config.rot_z);
    cv::Rodrigues(calibs_rot_vec, m_gyro_to_cam_rot_mat);
    m_frame_count = 0;
}

bool EIS::check_periodic_reset(std::vector<cv::Mat> &rolling_shutter_rotations, uint32_t curr_fps)
{
    /* If we haven't yet reached the hard deadline, check if this is a "good" time for reset:
        If all of the rotation matrices are close to the identity matrix, that way the reset will
        have less of a visual impact. Meaning:

        if frame_count in [EIS_RESET_FRAMES_NUM ,EIS_RESET_FRAMES_NUM + EIS_OPTIMAL_RESET_FRAMES_CHECK_NUM]:
            reset EIS only if all the rotation matrices are close to the identity matrix (all the angels are less then
       the threshold). if frame_count >= EIS_RESET_FRAMES_NUM + EIS_OPTIMAL_RESET_FRAMES_CHECK_NUM reset EIS
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

void EIS::reset_history()
{
    LOGGER__WARNING("[EIS] EIS reset!");
    previous_orientations.clear();
    previous_orientations.push(cv::Mat::eye(3, 3, CV_64F));
    prev_high_pass = {0, 0, 0, 0, 0, 0};
    m_prev_total_rotation = cv::Mat::eye(3, 3, CV_64F);
    m_cur_angle = cv::Vec3d(0.0, 0.0, 0.0);
    m_last_sample = unbiased_gyro_sample_t(0, 0, 0, 0);
    m_frame_count = 0;
}
