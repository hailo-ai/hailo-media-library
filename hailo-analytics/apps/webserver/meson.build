# Dependencies
thread_dep = dependency('threads')
glib_dep = dependency('glib-2.0')

common_src = [
  'resources/encoder.cpp',
  'resources/osd.cpp',
  'resources/configs_base.cpp',
  'resources/common/repository.cpp',
  'resources/common/resources.cpp',
  'resources/isp.cpp',
  'resources/common/isp/v4l2_ctrl.cpp',
  'resources/privacy_mask.cpp',
  'resources/webpage.cpp',
  'resources/webrtc.cpp',
  'resources/webrtc_turn.cpp',
  'resources/common/event_bus.cpp',
  'resources/common/events_utils.cpp',
  'resources/common/isp/common.cpp',
  'common/httplib/httplib_utils.cpp',
  'common/webserver_logger.cpp',
  'common/common.cpp',
]
src = common_src + [
  'resources/configs_medialib.cpp',
  'pipeline/base_pipeline.cpp',
  'pipeline/basic_pipeline.cpp',
  'pipeline/detection_pipeline.cpp',
  'pipeline/face_landmarks_pipeline.cpp',
  'pipeline/license_plate_pipeline.cpp',
  'pipeline/clip_pipeline.cpp',
  'pipeline/profile_manager_pipeline.cpp',
  'pipeline/dynamic_privacy_mask_pipeline.cpp',
  'pipeline/pipeline_factory.cpp',
  'pipeline/pipeline_event_handle.cpp',
  'pipeline/pipeline_endpoints.cpp',
  'pipeline/isp_blender.cpp'
]

webserver_deps = base_deps + [libhailort_dep, thread_dep, glib_dep, spdlog_dep, libdatachannel_link, clip_dep, libhailo_analytics_dep, face_landmarks_pipeline_builder_dep, lpr_pipeline_builder_dep, apps_common_dep]

webserver_lib = static_library('webserver',
  src,
  gnu_symbol_visibility : 'default',
  dependencies : webserver_deps,
  cpp_args: hailo_lib_args,
)

webserver_dep = declare_dependency(
  link_with : webserver_lib,
  dependencies : webserver_deps,
)

# Executable for GStreamer API
executable('camera-viewer-server',
  'main.cpp',
  gnu_symbol_visibility : 'default',
  dependencies : webserver_dep,
  cpp_args: hailo_lib_args,
  install: true,
  install_dir: get_option('bindir'),
)

# Profile Manager Viewer executable
executable('profile-manager-viewer',
  'profile_manager_viewer/main.cpp',
  gnu_symbol_visibility : 'default',
  dependencies : webserver_dep,
  cpp_args: hailo_lib_args,
  install: true,
  install_dir: get_option('bindir'),
)

camera_viewer_configs_install_dir = apps_install_dir + '/webserver/resources/configs/'
# Install config files to /home/root/apps/webserver/resources/configs/
config_files = run_command('find', 'configs', '-type', 'f', check: true).stdout().strip().split('\n')
install_data(
  config_files,
  install_dir: camera_viewer_configs_install_dir,
)
