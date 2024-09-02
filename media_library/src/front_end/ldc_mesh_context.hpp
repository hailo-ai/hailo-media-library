#pragma once
#include "dsp_utils.hpp"
#include "interface_types.h"
#include "config_manager.hpp"
#include "media_library_types.hpp"
#include "media_library_utils.hpp"
#include "hailo_v4l2/hailo_v4l2.h"
#include <memory>
#include <shared_mutex>
#include <tl/expected.hpp>
#include <opencv2/opencv.hpp>
#include <gyro_device.h>
#include "eis.hpp"

#define LDC_VSM_CONFIG "/usr/bin/media_server_cfg.json"

class LdcMeshContext
{
private:
    size_t m_input_width;
    size_t m_input_height;
    ldc_config_t m_ldc_configs;
    vsm_config_t m_vsm_config;
    uint64_t m_last_threshold_timestamp;
    // configuration manager
    std::shared_ptr<ConfigManager> m_config_manager;
    // Pointer to internally allocated DIS instance. used for DIS library mesh generation
    void *m_dis_ctx = nullptr;
    // dewarp mesh object
    dsp_dewarp_mesh_t m_dewarp_mesh;
    // Angular DIS
    std::shared_ptr<angular_dis_params_t> m_angular_dis_params;
    std::unique_ptr<EIS> m_eis_ptr;

    // optical zoom magnification level - used for dewarping
    float m_magnification;
    bool m_is_initialized = false;
    std::shared_mutex m_mutex;
    bool eis_prev_enabled = false;

    media_library_return
    initialize_dewarp_mesh();
    media_library_return initialize_dis_context();
    media_library_return free_dis_context();
    media_library_return free_angular_dis_resources();
    media_library_return initialize_angular_dis();
    media_library_return update_isp_vsm(struct hailo15_vsm &vsm);
    media_library_return angular_dis(struct hailo15_vsm &vsm);
    media_library_return read_vsm_config();
    FlipMirrorRot get_flip_value(flip_direction_t flip_dir, rotation_angle_t rotation_angle);
    tl::expected<dis_calibration_t, media_library_return> read_calibration_file(const char *name);

public:
    LdcMeshContext(ldc_config_t &config);
    ~LdcMeshContext();
    media_library_return configure(ldc_config_t &pre_proc_op_configs);
    media_library_return on_frame_vsm_update(struct hailo15_vsm &vsm);
    void on_frame_eis_update(uint64_t curr_frame_isp_timestamp_ns,
                             uint64_t integration_time,
                             bool enabled);
    media_library_return set_optical_zoom(float magnification);
    std::shared_ptr<angular_dis_params_t> get_angular_dis_params();
    dsp_dewarp_mesh_t *get();
};