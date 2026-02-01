# Sources
hailo_analytics_perfetto_sources = [
    'hailo_analytics_perfetto.cpp'
]

# Compile lib
hailo_analytics_perfetto_lib = static_library('hailo_analytics_perfetto',
    hailo_analytics_perfetto_sources,
    cpp_args: hailo_lib_args,
    include_directories: incdir,
    dependencies: [perfetto_dep],
    pic: true,
)

# Add to final lib components
hailo_analytics_components += [hailo_analytics_perfetto_lib]

# Create dep of this lib
hailo_analytics_perfetto_dep = declare_dependency(
  include_directories: [incdir],
  dependencies : [perfetto_dep],
  link_with : hailo_analytics_perfetto_lib)