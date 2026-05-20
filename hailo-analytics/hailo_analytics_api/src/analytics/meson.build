# Sources
analytics_sources = [
    'common_configs.cpp',
    'detection.cpp',
    'license_plate_recognition.cpp',
    'vision.cpp',
    'overlay.cpp',
    'analytic_metadata_zmq_sender.cpp',
    'analytic_metadata_ws_sender.cpp',
    'tiling.cpp',
    'face_landmarks.cpp',
    'face_recognition.cpp',
    'dynamic_privacy_mask.cpp',
    'dpm_analytics.cpp',
    'reference_camera_app_constructor.cpp'
]

# Compile lib
analytics_lib = static_library('hailo_analytics_pipelines',
    analytics_sources,
    cpp_args: hailo_lib_args,
    include_directories: incdir,
    dependencies: [base_deps, hailo_analytics_logger_dep, pipeline_dep],
    pic: true,
)

# Add to final lib components
hailo_analytics_components += [analytics_lib]

# Create dep of this lib
analytics_dep = declare_dependency(
  include_directories: [incdir],
  dependencies : [base_deps, hailo_analytics_logger_dep, pipeline_dep],
  link_with : analytics_lib)