# Sources
cropping_sources = [
    'dsp_cropping.cpp',
    'tiling_stage.cpp',
    'bbox_crop_stage.cpp',
    'aggregator_stage.cpp',
    'sync_aggregator_stage.cpp'
]

# Compile lib
cropping_lib = static_library('hailo_analytics_cropping',
    cropping_sources,
    cpp_args: hailo_lib_args + ['-Wno-missing-field-initializers'],
    include_directories: incdir,
    dependencies: [base_deps, hailo_analytics_logger_dep],
    pic: true,
)

# Add to final lib components
hailo_analytics_components += [cropping_lib]

# Create dep of this lib
cropping_dep = declare_dependency(
  include_directories: [incdir],
  dependencies : [base_deps, hailo_analytics_logger_dep],
  link_with : cropping_lib)
