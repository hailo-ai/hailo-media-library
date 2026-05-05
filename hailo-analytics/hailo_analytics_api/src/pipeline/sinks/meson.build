# Sources
sinks_sources = [
    'output_module.cpp',
    'udp_module.cpp',
    'udp_stage.cpp',
    'zmq_comm_stage.cpp',
    'convert_rtp_module.cpp',
    'rtp_converter_stage.cpp',
    'app_sink_stage.cpp',
    'websocket_sink_stage.cpp',
    'gst_sink_stage.cpp',
    'gsthailoroimeta.cpp'
]

# ZMQ dep
zeromq_dep = dependency('libzmq', method : 'pkg-config')

# libdatachannel dep (for websocket_sink_stage)
libdatachannel_dep = compiler.find_library('libdatachannel', required : true, dirs : '/usr/lib')

# Compile lib
sinks_lib = static_library('hailo_analytics_pipeline_sinks',
    sinks_sources,
    cpp_args: hailo_lib_args,
    include_directories: incdir,
    dependencies: [base_deps, hailo_analytics_logger_dep, zeromq_dep, libdatachannel_dep],
    pic: true,
)

# Add to final lib components
hailo_analytics_components += [sinks_lib]

# Create dep of this lib
pipeline_sinks_dep = declare_dependency(
  include_directories: [incdir],
  dependencies : [base_deps, hailo_analytics_logger_dep],
  link_with : sinks_lib)