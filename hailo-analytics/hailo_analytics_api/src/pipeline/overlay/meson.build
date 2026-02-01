# Sources
overlay_sources = [
    'overlay_stage.cpp'
]

# Compile lib
overlay_lib = static_library('hailo_analytics_overlay',
    overlay_sources,
    cpp_args: hailo_lib_args + ['-Wno-missing-field-initializers'],
    include_directories: incdir,
    dependencies: [base_deps, hailo_analytics_logger_dep],
    pic: true,
)

# Add to final lib components
hailo_analytics_components += [overlay_lib]

# Create dep of this lib
overlay_dep = declare_dependency(
  include_directories: [incdir],
  dependencies : [base_deps, hailo_analytics_logger_dep],
  link_with : overlay_lib)