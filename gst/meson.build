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
    'osd/osd_impl.cpp'
]

gstmedialibutils_lib = shared_library('gstmedialibutils',
    utils_sources,
    cpp_args : common_args,
    dependencies : gstreamer_deps +
                   [dsp_dep, media_library_common_dep, opencv_dep, spdlog_dep],
    gnu_symbol_visibility : 'default',
    version: meson.project_version(),
    install: true,
    install_dir: get_option('libdir'),
)

gstmedialibrary_utils_dep = declare_dependency(
  include_directories: [include_directories('./buffer_utils', './osd')],
  dependencies : [media_library_common_dep],
  link_with : gstmedialibutils_lib)


pkgc.generate(
    name : 'gstmedialibutils',
    libraries : gstmedialibutils_lib,
    subdirs : ['hailo_encoder', 'buffer_utils'],
    version : meson.project_version(),
    description : 'Gstreamer Media Library Utils',
)

install_headers('buffer_utils/buffer_utils.hpp')
install_headers('buffer_utils/gsthailobuffermeta.hpp')
install_headers('osd/osd_utils.hpp')
install_headers('osd/osd.hpp')

plugin_sources = [
    'gstmedialib.cpp',
    'visionpreproc/gsthailovisionpreproc.cpp',
    'encoder/gsthailoenc.cpp',
    'encoder/gsthailoh265enc.cpp',
    'encoder/gsthailoh264enc.cpp',
    'hailo_encoder/gsthailoencoder.cpp'
    ]

shared_library('gstmedialib',
    plugin_sources,
    cpp_args : common_args,
    link_args: ['-lhantro_vc8000e', '-lm'],
    dependencies : gstreamer_deps + [opencv_dep, rapidjson_dep] +
                   [dsp_dep, gstmedialibrary_utils_dep, encoder_dep,
                   media_library_common_dep, media_library_frontend_dep, media_library_encoder_dep],
    gnu_symbol_visibility : 'default',
    version: meson.project_version(),
    install: true,
    install_dir: get_option('libdir') + '/gstreamer-1.0/',
)

if get_option('include_unit_tests')
  subdir('tests')
endif
