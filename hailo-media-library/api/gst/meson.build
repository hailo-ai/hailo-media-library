################################################
# Thin GStreamer API plugin (gsthailovision + gsthailoencoder)
# Built AFTER libmedialib.so so all symbols are resolved.
################################################

# Generate config.h for this plugin (mirrors hailo-media-library/gst)
configure_file(output : 'config.h', configuration : core_conf)

version_arr = meson.project_version().split('.')
LIB_MAJOR_VERSION = version_arr[0].to_int()
LIB_MINOR_VERSION = version_arr[1].to_int()
gst_plugin_major_version_define = '-DGST_MEDIALIB_MAJOR_VERSION=@0@'.format(LIB_MAJOR_VERSION)
gst_plugin_minor_version_define = '-DGST_MEDIALIB_MINOR_VERSION=@0@'.format(LIB_MINOR_VERSION)

gst_api_sources = files(
  'utils/medialib_instance_registry.cpp',
  'utils/gst_pipeline_utils.cpp',
  'utils/buffer_forwarder.cpp',
  'vision/gsthailovision.cpp',
  'encoder/gsthailoencoder.cpp',
  'gstmedialib_api.cpp',
)

gst_api_include_dirs = [
  include_directories('.'),
  include_directories('utils'),
  include_directories('vision'),
  include_directories('encoder'),
]

shared_library('gstmedialib_api',
  gst_api_sources,
  cpp_args: common_args + [
    gst_plugin_major_version_define,
    gst_plugin_minor_version_define,
  ],
  include_directories: medialib_include_dirs + gst_api_include_dirs,
  dependencies: medialib_dependencies,
  link_with: libmedialib,
  install: true,
  install_dir: join_paths(get_option('libdir'), 'gstreamer-1.0'),
)
