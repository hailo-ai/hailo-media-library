if not dsp_dep.found()
    error('libhailodsp is required but not found')
endif

# Generate config.h file for GStreamer
configure_file(output : 'config.h', configuration : core_conf)

################################################
# Gstreamer Media Library
################################################

utils_sources = [
    'buffer_utils/buffer_utils.cpp',
    'buffer_utils/gsthailobuffermeta.cpp',
    'osd/osd.cpp',
    'osd/alignment.cpp',
    'utils/gsthailodmabufallocator.cpp',
    'utils/gsthailodmabufferpool.cpp',
    'common/gstmedialibptrs.cpp',
    'common/gstmedialibcommon.cpp',
    'osd/impl/blender_impl.cpp',
    'osd/impl/custom_overlay_impl.cpp',
    'osd/impl/datetime_overlay_impl.cpp',
    'osd/impl/image_overlay_impl.cpp',
    'osd/impl/overlay_impl.cpp',
    'osd/impl/text_overlay_impl.cpp',
    'osd/impl/simple_text_overlay_impl.cpp',
    'osd/impl/background_text_overlay_impl.cpp',
    
    # DSP Related sources
    'dsp/gsthailodspbufferpool.cpp',
    'dsp/gsthailodspbufferpoolutils.cpp',
    'dsp/gsthailodspbasetransform.cpp',
]

freetype_dep = dependency('freetype2', method : 'pkg-config', include_type : 'system')
harfbuzz_dep = dependency('harfbuzz', method : 'pkg-config', include_type : 'system')

gstmedialibutils_lib = shared_library('gstmedialibutils',
    utils_sources,
    cpp_args : common_args,
    link_args: ['-lm'],
    dependencies : gstreamer_deps + 
                   [dsp_dep, media_library_common_dep, opencv_dep, spdlog_dep, freetype_dep, harfbuzz_dep, fmt_dep],
    version: meson.project_version(),
    link_whole: git_metadata_lib,
    gnu_symbol_visibility : 'default',
    install: true,
)

gstmedialibrary_utils_dep = declare_dependency(
  include_directories: [include_directories('./buffer_utils', './osd', './common')],
  dependencies : [media_library_common_dep],
  link_with : gstmedialibutils_lib)


pkgc.generate(
    name : 'gstmedialibutils',
    libraries : gstmedialibutils_lib,
    subdirs : ['hailo_encoder', 'buffer_utils'],
    version : meson.project_version(),
    description : 'Gstreamer Media Library Utils',
)

install_headers('common/gstmedialibcommon.hpp')
install_headers('common/gstmedialibptrs.hpp')
install_headers('buffer_utils/buffer_utils.hpp')
install_headers('buffer_utils/gsthailobuffermeta.hpp')
install_headers('osd/osd.hpp')

headers_to_install = ['dsp/gsthailodspbasetransform.hpp',
                      'dsp/gsthailodspbufferpool.hpp', 
                      'dsp/gsthailodspbufferpoolutils.hpp']
install_headers(headers_to_install)

version_arr = meson.project_version().split('.')
LIB_MAJOR_VERSION = version_arr[0].to_int()
LIB_MINOR_VERSION = version_arr[1].to_int()
gst_plugin_major_version_define = '-DGST_MEDIALIB_MAJOR_VERSION=@0@'.format(LIB_MAJOR_VERSION)
gst_plugin_minor_version_define = '-DGST_MEDIALIB_MINOR_VERSION=@0@'.format(LIB_MINOR_VERSION)


plugin_sources = [
    'gstmedialib.cpp',
    'osd/gsthailoosd.cpp',
    'encoder/gsthailoenc.cpp',
    'encoder/gsthailoh265enc.cpp',
    'encoder/gsthailoh264enc.cpp',
    'hailo_encoder/gsthailoencoder.cpp',
    'jpeg/gsthailojpeg.cpp',
    'multi_resize/gsthailomultiresize.cpp',
    'dewarp/gsthailodewarp.cpp',
    'denoise/gstnativedenoise.cpp',
    'frontend/gsthailofrontend.cpp',
    'frontend/gsthailofrontendbinsrc.cpp',
    'encodebin/gsthailoencodebin.cpp',
    'upload/gsthailoupload.cpp',
    'image_freeze/gsthailoimagefreeze.cpp'
]

shared_library('gstmedialib',
    plugin_sources,
    cpp_args : common_args + [
        gst_plugin_major_version_define,
        gst_plugin_minor_version_define,
    ],
    link_args: ['-lhantro_vc8000e', '-lm'],
    dependencies : gstreamer_deps + [opencv_dep, rapidjson_dep] + hailort_dep +
                   [dsp_dep, gstmedialibrary_utils_dep, encoder_dep,
                   media_library_common_dep, media_library_frontend_dep, media_library_encoder_dep, fmt_dep],
    link_whole: git_metadata_lib,
    gnu_symbol_visibility : 'default',
    install: true,
    install_dir: get_option('libdir') / 'gstreamer-1.0',
)
