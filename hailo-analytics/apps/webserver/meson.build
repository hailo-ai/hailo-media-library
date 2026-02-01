# Dependencies
libdatachannel_link = compiler.find_library('libdatachannel', required : true, dirs : '/usr/lib')

thread_dep = dependency('threads')
glib_dep = dependency('glib-2.0')

common_src = [
  'resources/encoder.cpp',
  'resources/osd.cpp',
  'resources/configs_base.cpp',
  'resources/common/repository.cpp',
  'resources/isp.cpp',
  'resources/common/isp/v4l2_ctrl.cpp',
  'resources/privacy_mask.cpp',
  'resources/webpage.cpp',
  'resources/webrtc.cpp',
  'resources/common/event_bus.cpp',
  'resources/common/isp/common.cpp',
  'common/httplib/httplib_utils.cpp',
  'common/webserver_logger.cpp',
  'common/common.cpp'
]
src = common_src + [
  'resources/configs_medialib.cpp',
  'pipeline/base_pipeline.cpp',
  'pipeline/basic_pipeline.cpp',
  'pipeline/detection_pipeline.cpp',
  'pipeline/face_landmarks_pipeline.cpp',
  'pipeline/clip_pipeline.cpp',
  'pipeline/profile_manager_pipeline.cpp',
  'pipeline/pipeline_factory.cpp',
  'pipeline/pipeline_event_handle.cpp',
  'pipeline/pipeline_endpoints.cpp',
  'pipeline/isp_blender.cpp'
]

# Executable for GStreamer API
executable('camera-viewer-server',
  src + ['main.cpp'],
  gnu_symbol_visibility : 'default',
  dependencies : base_deps + [libhailort_dep, thread_dep, glib_dep, spdlog_dep, libdatachannel_link, clip_dep, libhailo_analytics_dep, ai_pipeline_builder_dep],
  cpp_args: hailo_lib_args,
  install: true,
  install_dir: get_option('bindir'),
)

# Profile Manager Viewer executable
executable('profile-manager-viewer',
  src + ['profile_manager_viewer/main.cpp'],
  gnu_symbol_visibility : 'default',
  dependencies : base_deps + [libhailort_dep, thread_dep, glib_dep, spdlog_dep, libdatachannel_link, clip_dep, libhailo_analytics_dep, ai_pipeline_builder_dep],
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
