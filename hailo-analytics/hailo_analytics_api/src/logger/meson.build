# Sources
hailo_analytics_logger_sources = [
    'hailo_analytics_logger.cpp'
]

# Compile lib
hailo_analytics_logger_lib = static_library('hailo_analytics_logger',
    hailo_analytics_logger_sources,
    cpp_args: hailo_lib_args,
    include_directories: incdir,
    dependencies: base_deps,
    pic: true,
)

# Add to final lib components
hailo_analytics_components += [hailo_analytics_logger_lib]

# Create dep of this lib
hailo_analytics_logger_dep = declare_dependency(
  include_directories: [incdir],
  dependencies : base_deps,
  link_with : hailo_analytics_logger_lib)