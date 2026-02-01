# Sources
routing_sources = [
    'tee_stage.cpp',
    'callback_stage.cpp',
    'valve_stage.cpp',
    'freeze_stage.cpp',
    'tracker_traffic_ctrl_stage.cpp'
]

# Compile lib
routing_lib = static_library('hailo_analytics_routing',
    routing_sources,
    cpp_args: hailo_lib_args,
    include_directories: incdir,
    dependencies: [base_deps, hailo_analytics_logger_dep],
    pic: true,
)

# Add to final lib components
hailo_analytics_components += [routing_lib]

# Create dep of this lib
routing_dep = declare_dependency(
  include_directories: [incdir],
  dependencies : [base_deps, hailo_analytics_logger_dep],
  link_with : routing_lib)