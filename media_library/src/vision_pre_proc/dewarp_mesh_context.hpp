#pragma once
#include "dsp_utils.hpp"
#include "interface_types.h"
#include "media_library_types.hpp"
#include "media_library_utils.hpp"
#include "hailo_v4l2/hailo_vsm.h"
#include <shared_mutex>
#include <tl/expected.hpp>

class DewarpMeshContext
{
private:
    size_t m_input_width;
    size_t m_input_height;
    pre_proc_op_configurations m_pre_proc_configs;
    // Pointer to internally allocated DIS instance. used for DIS library mesh generation
    void *m_dis_ctx = nullptr;
    // dewarp mesh object
    dsp_dewarp_mesh_t m_dewarp_mesh;
    // optical zoom magnification level - used for dewarping
    float m_magnification;
    bool m_is_initialized = false;
    std::shared_mutex m_mutex;

    media_library_return
    initialize_dewarp_mesh();
    media_library_return initialize_dis_context();
    media_library_return free_dis_context();
    FlipMirrorRot get_flip_value(flip_direction_t flip_dir, rotation_angle_t rotation_angle);
    tl::expected<dis_calibration_t, media_library_return> read_calibration_file(const char *name);

public:
    size_t m_dewarp_output_width;
    size_t m_dewarp_output_height;

    DewarpMeshContext(pre_proc_op_configurations &config);
    ~DewarpMeshContext();
    media_library_return configure(pre_proc_op_configurations &pre_proc_op_configs);
    media_library_return on_frame_vsm_update(struct hailo15_vsm &vsm);
    media_library_return set_optical_zoom(float magnification);
    dsp_dewarp_mesh_t *get();
};