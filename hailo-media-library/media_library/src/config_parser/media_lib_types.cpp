#include <imaging/aaa_config_types.hpp>
#include <string>

#include "media_library_types.hpp"
#include "json_flattener.hpp"
#include "media_library_logger.hpp"
#include "media_library_rule_checker.hpp"
#include "dis_common.h"

#define MODULE_NAME LoggerType::Config

// Initialize is_persistent members with default values
bool dewarp_config_t::is_persistent = true;
bool digital_zoom_config_t::is_persistent = true;
bool optical_zoom_config_t::is_persistent = true;
bool flip_config_t::is_persistent = true;
bool rotation_config_t::is_persistent = true;
bool motion_detection_config_t::is_persistent = true;
bool application_analytics_config_t::is_persistent = true;
bool eis_config_t::is_persistent = true;
bool gyro_config_t::is_persistent = true;
bool privacy_mask_config_t::is_persistent = true;
bool dynamic_privacy_mask_config_t::is_persistent = false;
bool config_application_settings_t::is_persistent = true;
bool config_stabilizer_settings_t::is_persistent = true;
bool config_iq_settings_t::is_persistent = true;
bool config_stream_osd_t::is_persistent = true;
bool dis_config_t::is_persistent = true;
output_resolution_t::override_policy output_resolution_t::persistent_fields = {
    .framerate = false,
    .pool_max_buffers = false,
    .dimensions = true,
    .scaling_mode = true,
};
bool config_application_input_streams_t::is_persistent = true;
bool config_encoded_output_stream_t::is_persistent = true;
bool config_sensor_config_t::is_persistent = false;
bool denoise_config_t::is_persistent = false;
bool hdr_config_t::is_persistent = false;

bool bayer_network_config_t::operator==(const bayer_network_config_t &other) const = default;

// Initialize persistence members for automatic_algorithms_config_t structs
bool automatic_algorithms_config_t::is_recursively_persistent = false;
bool reload_mode_config_t::is_persistent = false;
bool Aev1_config_t::is_persistent = false;
bool Aev2_config_t::is_persistent = false;
bool Aehdr_config_t::is_persistent = false;
bool AdaptiveAe_config_t::is_persistent = false;
bool HDRAdaptiveAE_config_t::is_persistent = false;
bool Awbv2_config_t::is_persistent = false;
bool Afv1_config_t::is_persistent = false;
bool IspController_config_t::is_persistent = false;
bool AutoHdr_config_t::is_persistent = false;
bool DciHist_config_t::is_persistent = false;
bool SensorController_config_t::is_persistent = false;
bool AGamma64_config_t::is_persistent = false;
bool ACproc_config_t::is_persistent = false;
bool ACprocPost_config_t::is_persistent = false;
bool Aeev1_config_t::is_persistent = false;
bool ADmscv2_config_t::is_persistent = false;
bool AWdrv4_config_t::is_persistent = false;
bool A3dnrv1_config_t::is_persistent = false;
bool A2dnrv5_config_t::is_persistent = false;
bool ADpf_config_t::is_persistent = false;
bool ABls_config_t::is_persistent = false;
bool ALsc_config_t::is_persistent = false;

media_library_return profile_t::flatten_n_validate_config()
{
    JsonParser parser;
    LOGGER__MODULE__INFO(MODULE_NAME, "Flattening and validating profile named: {}, in file: {}", name, config_file);
    auto status = parser.flatten_profile(config_file, flattened_config_file_content);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to flatten and validate profile named: {}, in file: {}", name,
                              config_file);
        return status;
    }
    LOGGER__MODULE__INFO(
        MODULE_NAME, "Profile '{}' in file '{}' was successfully flattened and validated. Proceeding to rule checking.",
        name, config_file);
    MediaLibraryRuleChecker rule_checker;
    status = rule_checker.validate_config(flattened_config_file_content);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Rule checker validation failed for profile named: {}, in file: {}", name,
                              config_file);
        return status;
    }
    return MEDIA_LIBRARY_SUCCESS;
}
