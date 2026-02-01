# Sources
utils_sources = [
    'platform_utils.cpp',
    'env_utils.cpp',
]

# Compile lib
utils_lib = static_library('hailo_analytics_utils',
    utils_sources,
    cpp_args: hailo_lib_args,
    include_directories: incdir,
    dependencies: [base_deps],
    pic: true,
)

# Add to final lib components
hailo_analytics_components += [utils_lib]

# Create dep of this lib
utils_dep = declare_dependency(
  include_directories: [incdir],
  dependencies : [base_deps],
  link_with : utils_lib)
