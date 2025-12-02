# Sources
analytics_logger_sources = [
    'hailo_analytics_logger.cpp'
]

# Compile lib
hailo_analytics_logger_lib = shared_library('hailo_analytics_logger',
    analytics_logger_sources,
    cpp_args: hailo_lib_args,
    include_directories: [incdir],
    dependencies : apps_dependencies + [hailo_analytics_perfetto_dep, spdlog_dep],
    version: meson.project_version(),
    install: true,
    install_dir: get_option('libdir'),
)

# Create dep of this lib
hailo_analytics_logger_dep = declare_dependency(
  include_directories: [incdir],
  dependencies : [hailo_analytics_perfetto_dep, spdlog_dep],
  link_with : hailo_analytics_logger_lib)

# Gnerate pkgc-config for this lib
pkgc.generate(name: 'hailo_analytics_logger',
              libraries: hailo_analytics_logger_lib,
              subdirs: 'hailo',
              version: meson.project_version(),
              description: 'Hailo Analytics Logger',
              requires: ['spdlog']
)