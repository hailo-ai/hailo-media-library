# Sources
codecs_sources = [
    'encoder_stage.cpp',
    'analytic_metadata_packager_stage.cpp'
]

# Compile lib
codecs_lib = static_library('hailo_analytics_pipeline_codecs',
    codecs_sources,
    cpp_args: hailo_lib_args,
    include_directories: incdir,
    dependencies: [base_deps, hailo_analytics_logger_dep],
    pic: true,
)

# Add to final lib components
hailo_analytics_components += [codecs_lib]

# Create dep of this lib
pipeline_codecs_dep = declare_dependency(
  include_directories: [incdir],
  dependencies : [base_deps, hailo_analytics_logger_dep],
  link_with : codecs_lib)