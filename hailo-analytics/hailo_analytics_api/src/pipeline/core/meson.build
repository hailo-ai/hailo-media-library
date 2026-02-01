# Sources
pipeline_sources = [
    'buffer.cpp',
    'queue.cpp',
    'stage_tracing.cpp',
    'stage.cpp',
    'pipeline.cpp',
    'pipeline_builder.cpp',
    'pipeline_exporter.cpp'
]

# Compile lib
pipeline_lib = static_library('hailo_analytics_pipeline',
    pipeline_sources,
    cpp_args: hailo_lib_args,
    include_directories: incdir,
    dependencies: [base_deps, hailo_analytics_logger_dep],
    pic: true,
)

# Add to final lib components
hailo_analytics_components += [pipeline_lib]

# Create dep of this lib
pipeline_dep = declare_dependency(
  include_directories: [incdir],
  dependencies : [base_deps, hailo_analytics_logger_dep],
  link_with : pipeline_lib)