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
    'osd/gsthailoosd.cpp',
    'osd/osd.cpp',
    'utils/gsthailodmabufferpoolutils.cpp',
    'utils/gsthailodmabufallocator.cpp',
    'utils/gsthailodmabufferpool.cpp',
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

opencv_freetype_lib_dep = meson.get_compiler('cpp').find_library('opencv_freetype', required: true, dirs: '/usr/lib/')
v4l_dep = [
    meson.get_compiler('cpp').find_library('libgstvideo4linux2', required: true, dirs: sysroot + '/usr/lib/gstreamer-1.0/')
]

gstmedialibutils_lib = shared_library('gstmedialibutils',
    utils_sources,
    cpp_args : common_args,
    link_args: ['-lm', '-Wl,-rpath,/usr/lib/gstreamer-1.0'],
    dependencies : gstreamer_deps + 
                   [dsp_dep, media_library_common_dep, opencv_dep, spdlog_dep, opencv_freetype_lib_dep, v4l_dep],
    gnu_symbol_visibility : 'default',
    version: meson.project_version(),
    install: true,
    install_dir: get_option('libdir'),
    install_rpath: '/usr/lib/gstreamer-1.0',
)

gstmedialibrary_utils_dep = declare_dependency(
  include_directories: [include_directories('./buffer_utils', './osd')],
  dependencies : [media_library_common_dep, v4l_dep],
  link_with : gstmedialibutils_lib)


pkgc.generate(
    name : 'gstmedialibutils',
    libraries : gstmedialibutils_lib,
    subdirs : ['hailo_encoder', 'buffer_utils'],
    version : meson.project_version(),
    description : 'Gstreamer Media Library Utils',
)

install_headers('common/gstmedialibcommon.hpp')
install_headers('buffer_utils/buffer_utils.hpp')
install_headers('buffer_utils/gsthailobuffermeta.hpp')
install_headers('osd/osd.hpp')

headers_to_install = ['dsp/gsthailodspbasetransform.hpp',
                      'dsp/gsthailodspbufferpool.hpp', 'dsp/gsthailodspbufferpoolutils.hpp']
install_headers(headers_to_install)


plugin_sources = [
    'gstmedialib.cpp',
    'visionpreproc/gsthailovisionpreproc.cpp',
    'encoder/gsthailoenc.cpp',
    'encoder/gsthailoh265enc.cpp',
    'encoder/gsthailoh264enc.cpp',
    'hailo_encoder/gsthailoencoder.cpp',
    'jpeg/gsthailojpeg.cpp',
    'multi_resize/gsthailomultiresize.cpp',
    'dewarp/gsthailodewarp.cpp',
    'denoise/gstnativedenoise.cpp',
    'defog/gsthailodefog.cpp',
    'frontend/gsthailofrontend.cpp',
    'frontend/gsthailofrontendbinsrc.cpp',
    'frontend/hdr.cpp',
    'encodebin/gsthailoencodebin.cpp',
    'upload/gsthailoupload.cpp'
]

if get_option('hailort_4_16')
    message('Building gstmedialib with Hailort 4.16')
    plugin_sources += [
    ]
    common_args += ['-DHAILORT_4_16']
endif

hailort_inc_dirs = include_directories(
    sysroot + '/usr/include/hailo',
    sysroot + '/usr/include/gst-hailo',
    is_system: true
)

shared_library('gstmedialib',
    plugin_sources,
    cpp_args : common_args,
    link_args: ['-lhantro_vc8000e', '-lm'],
    include_directories: hailort_inc_dirs,
    dependencies : gstreamer_deps + [opencv_dep, opencv_freetype_lib_dep, rapidjson_dep] + hailort_dep +
                   [dsp_dep, gstmedialibrary_utils_dep, encoder_dep,
                   media_library_common_dep, media_library_frontend_dep, media_library_encoder_dep],
    gnu_symbol_visibility : 'default',
    version: meson.project_version(),
    install: true,
    install_dir: get_option('libdir') / 'gstreamer-1.0',
    install_rpath: '/usr/lib/gstreamer-1.0',
)

if get_option('include_unit_tests')
  subdir('tests')
endif
