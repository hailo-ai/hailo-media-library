# Sources
pipeline_infra_sources = [
    'buffer.cpp',
    'queue.cpp',
    'stage_tracing.cpp',
    'stage.cpp',
    'pipeline.cpp'
]

# Compile lib
pipeline_infra_lib = shared_library('hailo_analytics_pipeline_infra',
    pipeline_infra_sources,
    cpp_args: hailo_lib_args,
    include_directories: [incdir],
    dependencies : apps_dependencies + [hailo_analytics_perfetto_dep, hailo_analytics_logger_dep],
    version: meson.project_version(),
    install: true,
    install_dir: get_option('libdir'),
)

# Create dep of this lib
pipeline_infra_dep = declare_dependency(
  include_directories: [incdir],
  dependencies : [hailo_analytics_perfetto_dep, hailo_analytics_logger_dep],
  link_with : pipeline_infra_lib)

# Gnerate pkgc-config for this lib
pkgc.generate(name: 'hailo_analytics_pipeline_infra',
              libraries: pipeline_infra_lib,
              subdirs: 'hailo',
              version: meson.project_version(),
              description: 'Hailo Analytics Pipeline Infra',
              requires: ['hailo_analytics_logger_dep']
)