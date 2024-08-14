#include <fstream>
#include <nlohmann/json.hpp>
#include "media_library_logger.hpp"
#include "eis.hpp"


static cv::Mat SLERP(const cv::Mat& r1, const cv::Mat& r2, double rotational_smoothing_coefficient)
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

    for (size_t i = 1; i < previous_orientations.size(); i++) {
        smooth_orientation = SLERP(smooth_orientation, previous_orientations[i], rotational_smoothing_coefficient);
    }

    smooth_orientation = SLERP(smooth_orientation, current_orientation, rotational_smoothing_coefficient);
    previous_orientations.push(current_orientation.clone());

    return smooth_orientation;
}

static inline cv::Mat get_curr_gyro_rotation_mat(const unbiased_gyro_sample_t &gyro_sample,
                                                 uint64_t start_time, uint64_t end_time)
{
    /* Convert to nanoseconds */
    double delta_t = ((double)end_time - (double)start_time) * 1e-9;
    if (delta_t <= 0.0)
    {
        /* In the case when a gyro sample is not synchronised disregard it */
        return cv::Mat::eye(3, 3, CV_64F);
    }
    cv::Vec3d gyro_scaled_rot_vec(gyro_sample.vx * delta_t,
                                  gyro_sample.vy * delta_t,
                                  gyro_sample.vz * delta_t);
    cv::Mat gyro_rot_mat;
    cv::Rodrigues(gyro_scaled_rot_vec, gyro_rot_mat);
    return gyro_rot_mat;
}

cv::Mat EIS::integrate_rotations(uint64_t last_frame_gyro_ts,
                                 const std::vector<unbiased_gyro_sample_t> &frame_gyro_records)
{
    if (frame_gyro_records.size() == 0)
    {
        return prev_gyro_orientation.clone();
    }
    cv::Mat gyro_adjusted_orientation =
        prev_gyro_orientation * get_curr_gyro_rotation_mat(frame_gyro_records[0],
                                                            last_frame_gyro_ts, frame_gyro_records[0].timestamp_ns);
    for (size_t i = 1; i < frame_gyro_records.size(); i++)
    {
        gyro_adjusted_orientation *= get_curr_gyro_rotation_mat(frame_gyro_records[i],
                                                                frame_gyro_records[i - 1].timestamp_ns, frame_gyro_records[i].timestamp_ns);
    }

    prev_gyro_orientation = gyro_adjusted_orientation.clone();

    cv::Vec3d calibs_rot_vec(m_gyro_calibration_config.rot_x, m_gyro_calibration_config.rot_y, m_gyro_calibration_config.rot_z);
    cv::Mat calib_rot_mat;
    cv::Rodrigues(calibs_rot_vec, calib_rot_mat);
    gyro_adjusted_orientation = (calib_rot_mat * gyro_adjusted_orientation) * calib_rot_mat.t();

    return gyro_adjusted_orientation;
}

void EIS::remove_bias(const std::vector<gyro_sample_t>& gyro_records,
                      std::vector<unbiased_gyro_sample_t>& unbiased_records,
                      double gyro_scale,
                      double iir_hpf_coefficient)
{
    /* Precompute invariant part of the filter formula 
       y_n = iir_hpf_coefficient * y_(n-1) + (1 + iir_hpf_coefficient) / 2 * (x_n - x_(n-1))
       y_curr = iir_hpf_coefficient * y_prev + ((1 + iir_hpf_coefficient) / 2) * (x_curr - x_prev) */
    double one_plus_beta_over_two = (1 + iir_hpf_coefficient) / 2;

    unbiased_records.clear();
    unbiased_records.reserve(gyro_records.size());

    for (const auto& record : gyro_records)
    {
        /* Use references to reduce struct member access overhead */
        double vx = static_cast<double>(record.vx) * gyro_scale;
        double vy = static_cast<double>(record.vy) * gyro_scale;
        double vz = static_cast<double>(record.vz) * gyro_scale;

        /* Apply bias correction */
        vx -= static_cast<double>(m_gyro_calibration_config.gbias_x);
        vy -= static_cast<double>(m_gyro_calibration_config.gbias_y);
        vz -= static_cast<double>(m_gyro_calibration_config.gbias_z);

        /* Apply high pass filter */
        double new_smooth_x = iir_hpf_coefficient * prev_high_pass.prev_smooth_x + one_plus_beta_over_two * (vx - prev_high_pass.prev_gyro_x);
        double new_smooth_y = iir_hpf_coefficient * prev_high_pass.prev_smooth_y + one_plus_beta_over_two * (vy - prev_high_pass.prev_gyro_y);
        double new_smooth_z = iir_hpf_coefficient * prev_high_pass.prev_smooth_z + one_plus_beta_over_two * (vz - prev_high_pass.prev_gyro_z);

        /* Update previous values for the next iteration */
        prev_high_pass.prev_gyro_x = vx;
        prev_high_pass.prev_gyro_y = vy;
        prev_high_pass.prev_gyro_z = vz;

        prev_high_pass.prev_smooth_x = new_smooth_x;
        prev_high_pass.prev_smooth_y = new_smooth_y;
        prev_high_pass.prev_smooth_z = new_smooth_z;

        /* Update the record with the new smoothed values */
        unbiased_records.emplace_back(new_smooth_x, new_smooth_y, new_smooth_z, record.timestamp_ns);
    }
}

static int parse_gyro_calibration_config_file(const std::string &filename, gyro_calibration_config_t &gyro_calibration_config)
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
        {"gbias_x", &gyro_calibration_config_t::gbias_x},
        {"gbias_y", &gyro_calibration_config_t::gbias_y},
        {"gbias_z", &gyro_calibration_config_t::gbias_z},
        {"rot_x", &gyro_calibration_config_t::rot_x},
        {"rot_y", &gyro_calibration_config_t::rot_y},
        {"rot_z", &gyro_calibration_config_t::rot_z},
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
    prev_high_pass = {0,0,0,0,0,0};
    prev_gyro_orientation = cv::Mat::eye(3, 3, CV_64F);
}

void EIS::reset()
{
    previous_orientations.clear();
    previous_orientations.push(cv::Mat::eye(3, 3, CV_64F));
    prev_high_pass = {0,0,0,0,0,0};
    prev_gyro_orientation = cv::Mat::eye(3, 3, CV_64F);
}
