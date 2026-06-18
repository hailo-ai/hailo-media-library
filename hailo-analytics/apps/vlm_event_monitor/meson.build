################################################
# HAILO 15 — VLM Event Monitor
################################################

# External dependencies
libdatachannel_link = compiler.find_library('libdatachannel', required : true, dirs : '/usr/lib')
yaml_cpp_dep = dependency('yaml-cpp', required : true)
# libjpeg-turbo — used only by the debug-only metadata writer.
libjpeg_link = compiler.find_library('jpeg', required : true)

vlm_event_monitor_install_dir = apps_install_dir + '/vlm_event_monitor'

################################################
# VLM Inference Manager
################################################
vlm_inference_manager_lib = static_library('vlm_inference_manager',
    'custom/inference/vlm_inference_manager.cpp',
    cpp_args : hailo_lib_args,
    dependencies : [libhailo_analytics_dep],
    include_directories : include_directories('custom/inference'),
    gnu_symbol_visibility : 'default',
)

vlm_inference_manager_dep = declare_dependency(
    link_with : vlm_inference_manager_lib,
    dependencies : [libhailo_analytics_dep],
    include_directories : include_directories('custom/inference'),
)

################################################
# VLM Frame Preprocessor (Used only by the console diag tool)
################################################
vlm_frame_preprocessor_lib = static_library('vlm_frame_preprocessor',
    'apps/vlm_console/vlm_frame_preprocessor.cpp',
    cpp_args : hailo_lib_args,
    dependencies : [libhailo_analytics_dep, opencv_dep],
    include_directories : include_directories('apps/vlm_console'),
    gnu_symbol_visibility : 'default',
)

vlm_frame_preprocessor_dep = declare_dependency(
    link_with : vlm_frame_preprocessor_lib,
    dependencies : [libhailo_analytics_dep, opencv_dep],
    include_directories : include_directories('apps/vlm_console'),
)

################################################
# Main VLM Event Monitor library
################################################
vlm_event_monitor_lib_src = [
    'apps/vlm_pipeline.cpp',
    'apps/webserver.cpp',
    'custom/streaming/webrtc_streamer_ext.cpp',
    'custom/pipeline/jpeg_ring_buffer_stage.cpp',
    'custom/pipeline/nv12_to_rgb_stage.cpp',
    'custom/event_monitor/prompt_builder.cpp',
    'custom/event_monitor/inference_request_queue.cpp',
    'custom/event_monitor/event_check_runner.cpp',
    'custom/event_monitor/event_store.cpp',
    'custom/event_monitor/event_state_tracker.cpp',
    'custom/event_monitor/metadata_writer.cpp',
    'custom/event_monitor/chat_session_broker.cpp',
    'custom/service/sse_broadcaster.cpp',
    'utils/vlm_app_config_parser.cpp',
]

vlm_event_monitor_include_dirs = include_directories('.', 'apps', 'utils', 'custom')

vlm_event_monitor_lib = static_library('vlm_event_monitor',
    vlm_event_monitor_lib_src,
    cpp_args : hailo_lib_args,
    dependencies : [
        libdatachannel_link,
        yaml_cpp_dep,
        libjpeg_link,
        libhailo_analytics_dep,
        vlm_inference_manager_dep,
        vlm_frame_preprocessor_dep,
    ],
    gnu_symbol_visibility : 'default',
    include_directories : vlm_event_monitor_include_dirs,
    install : false,
)

vlm_event_monitor_dep = declare_dependency(
    link_with : vlm_event_monitor_lib,
    dependencies : [
        libdatachannel_link,
        yaml_cpp_dep,
        libjpeg_link,
        libhailo_analytics_dep,
        vlm_inference_manager_dep,
        vlm_frame_preprocessor_dep,
    ],
    include_directories : vlm_event_monitor_include_dirs,
)

################################################
# Main executable: links the lib + main.cpp.
################################################
executable('vlm_event_monitor',
    'main.cpp',
    cpp_args : hailo_lib_args,
    dependencies : [vlm_event_monitor_dep],
    gnu_symbol_visibility : 'default',
    install : true,
    install_dir : vlm_event_monitor_install_dir,
    include_directories : vlm_event_monitor_include_dirs,
)

################################################
# Console diag tool
################################################
executable('vlm_console',
    'apps/vlm_console/main.cpp',
    cpp_args : hailo_lib_args,
    dependencies : [vlm_inference_manager_dep, vlm_frame_preprocessor_dep],
    gnu_symbol_visibility : 'default',
    install : true,
    install_dir : vlm_event_monitor_install_dir / 'console',
)

################################################
# Install resources
################################################
install_data(
    sources : [
        'resources/configs/vlm_app_config.yaml',
        'resources/configs/vlm_event_monitor_events.yaml',
    ],
    install_dir : vlm_event_monitor_install_dir / 'resources/configs',
)

install_data(
    sources : [
        'resources/webfrontend/index.html',
        'resources/webfrontend/jszip.min.js',
    ],
    install_dir : vlm_event_monitor_install_dir / 'resources/webfrontend',
)
