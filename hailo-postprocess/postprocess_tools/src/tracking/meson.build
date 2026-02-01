# Sources
tracker_sources = [
    'hailo_tracker.cpp',
    'hailo_lightweight_tracker.cpp'
]

# Compile lib
tracker_lib = static_library('hailo_tracker',
    tracker_sources,
    cpp_args: hailo_lib_args,
    include_directories: incdir,
    dependencies: base_deps,
    pic: true,
)

# Add to final lib components
hailo_postprocess_tools_components += [tracker_lib]

# Create dep of this lib
tracker_dep = declare_dependency(
  include_directories: [incdir],
  dependencies : base_deps,
  link_with : tracker_lib)