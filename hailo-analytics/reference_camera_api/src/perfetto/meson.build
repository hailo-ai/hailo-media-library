# Sources
hailo_analytics_perfetto_sources = [
    'hailo_analytics_perfetto.cpp'
]

# Compile lib
hailo_analytics_perfetto_lib = shared_library('hailo_analytics_perfetto',
    hailo_analytics_perfetto_sources,
    cpp_args: hailo_lib_args,
    include_directories: [incdir],
    dependencies : [perfetto_dep],
    version: meson.project_version(),
    install: true,
    install_dir: get_option('libdir'),
)

# Create dep of this lib
hailo_analytics_perfetto_dep = declare_dependency(
  include_directories: [incdir],
  dependencies : [perfetto_dep],
  link_with : hailo_analytics_perfetto_lib)

# Gnerate pkgc-config for this lib
pkgc.generate(name: 'hailo_analytics_perfetto',
              libraries: hailo_analytics_perfetto_lib,
              subdirs: 'hailo',
              version: meson.project_version(),
              description: 'Hailo Analytics Perfetto',
              requires: ['spdlog']
)