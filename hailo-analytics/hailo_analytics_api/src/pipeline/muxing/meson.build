# Sources
muxing_sources = [
    'muxer_stage.cpp',
    'demuxer_stage.cpp',
    'bundle_streams_stage.cpp',
    'split_streams_stage.cpp'
]

# Compile lib
muxing_lib = static_library('hailo_analytics_muxing',
    muxing_sources,
    cpp_args: hailo_lib_args,
    include_directories: incdir,
    dependencies: [base_deps, hailo_analytics_logger_dep],
    pic: true,
)

# Add to final lib components
hailo_analytics_components += [muxing_lib]

# Create dep of this lib
muxing_dep = declare_dependency(
  include_directories: [incdir],
  dependencies : [base_deps, hailo_analytics_logger_dep],
  link_with : muxing_lib)