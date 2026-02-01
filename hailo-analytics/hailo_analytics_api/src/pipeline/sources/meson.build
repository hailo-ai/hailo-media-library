# Sources
sources_sources = [
    'frontend_stage.cpp',
    'file_reader_module.cpp',
    'file_source_stage.cpp',
    'frontend_stage_from_file.cpp'
]

# Compile lib
sources_lib = static_library('hailo_analytics_pipeline_sources',
    sources_sources,
    cpp_args: hailo_lib_args,
    include_directories: incdir,
    dependencies: [base_deps, hailo_analytics_logger_dep],
    pic: true,
)

# Add to final lib components
hailo_analytics_components += [sources_lib]

# Create dep of this lib
pipeline_sources_dep = declare_dependency(
  include_directories: [incdir],
  dependencies : [base_deps, hailo_analytics_logger_dep],
  link_with : sources_lib)