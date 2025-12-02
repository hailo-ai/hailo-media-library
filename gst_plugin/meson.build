# This must be built AFTER libmedialib.so because it depends on libmedialib_dep

# Encoder link arguments for plugin
encoder_link_args = ['-lhantro_vc8000e', '-lm']

version_arr = meson.project_version().split('.')
LIB_MAJOR_VERSION = version_arr[0].to_int()
LIB_MINOR_VERSION = version_arr[1].to_int()
gst_plugin_major_version_define = '-DGST_MEDIALIB_MAJOR_VERSION=@0@'.format(LIB_MAJOR_VERSION)
gst_plugin_minor_version_define = '-DGST_MEDIALIB_MINOR_VERSION=@0@'.format(LIB_MINOR_VERSION)

plugin_sources = files(
  '../gst/gstmedialib.cpp',
  '../gst/osd/gsthailoosd.cpp',
  '../gst/encoder/gsthailoenc.cpp',
  '../gst/encoder/gsthailoh265enc.cpp',
  '../gst/encoder/gsthailoh264enc.cpp',
  '../gst/hailo_encoder/gsthailoencoder.cpp',
  '../gst/jpeg/gsthailojpeg.cpp',
  '../gst/multi_resize/gsthailomultiresize.cpp',
  '../gst/dewarp/gsthailodewarp.cpp',
  '../gst/denoise/gstnativedenoise.cpp',
  '../gst/frontend/gsthailofrontend.cpp',
  '../gst/frontend/gsthailofrontendbinsrc.cpp',
  '../gst/encodebin/gsthailoencodebin.cpp',
  '../gst/upload/gsthailoupload.cpp',
  '../gst/image_freeze/gsthailoimagefreeze.cpp'
)

gst_plugin_include_dirs = [
  include_directories('../gst'),
  include_directories('../gst/buffer_utils'),
  include_directories('../gst/osd'),
  include_directories('../gst/common'),
  include_directories('../gst/dsp'),
]

shared_library('gstmedialib',
  plugin_sources,
  cpp_args: common_args + [
    gst_plugin_major_version_define,
    gst_plugin_minor_version_define,
  ],
  link_args: encoder_link_args,
  link_whole: git_metadata_lib,
  include_directories: gst_plugin_include_dirs,
  dependencies: libmedialib_dep,
  install: true,
  install_dir: get_option('libdir') / 'gstreamer-1.0',
)
