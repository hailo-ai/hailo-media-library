# Sources
hailo_postprocess_logger_src = [
    'hailo_postprocess_logger.cpp'
]

# Compile lib
hailo_postprocess_logger_lib = static_library('hailo_postprocess_logger',
    hailo_postprocess_logger_src,
    cpp_args: hailo_lib_args,
    include_directories: incdir,
    dependencies: [base_deps],
    pic: true,
)

# Add to final lib components
hailo_postprocess_tools_components += [hailo_postprocess_logger_lib]

# Create dep of this lib
hailo_postprocess_logger_dep = declare_dependency(
  include_directories: [incdir],
  dependencies : [base_deps],
  link_with : hailo_postprocess_logger_lib)
