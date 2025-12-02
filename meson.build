# Project Declaration
project('media-library', 'c', 'cpp',
    version : '1.9.0',
    default_options : [ 'warning_level=2', 'werror=true',
                        'buildtype=' + get_option('buildtype'),
                        'c_std=c11', 'cpp_std=c++20']
)

# Compiler Arguments
add_global_arguments('-Werror', language : 'cpp')

pkgc = import('pkgconfig')
build_docs = get_option('build_docs')

if build_docs
    subdir('docs')
    subdir_done()
endif

subdir('common_meson')

# Gstreamer Dependencies
gst_req = '>= 1.0.0'
gst_dep = dependency('gstreamer-1.0', required: true, version : gst_req)
gst_base_dep = dependency('gstreamer-base-1.0', required: true, version : gst_req)
gst_app_dep = dependency('gstreamer-app-1.0', required: true, version : gst_req)
gstvideo_dep = dependency('gstreamer-video-1.0', required: true, version : gst_req)
gstallocators_dep = dependency('gstreamer-allocators-1.0', required: true, version : gst_req)
gstv4l2_dep = dependency('gstvideo4linux2', required: true)
gstreamer_deps = [gst_dep, gst_base_dep, gstvideo_dep, gst_app_dep, gstallocators_dep, gstv4l2_dep]

libiio_dep = dependency('libiio', version: '>=0.19', required: true)
freetype_dep = dependency('freetype2', method: 'pkg-config', include_type: 'system')
harfbuzz_dep = dependency('harfbuzz', method: 'pkg-config', include_type: 'system')
rapidjson_dep = dependency('RapidJSON', method : 'pkg-config')
json_dep = meson.get_compiler('cpp').find_library('libnlohmann_json_schema_validator', required: true, dirs: '/usr/lib/')
opencv_dep = dependency('opencv4', version : '>= 4.0', method : 'pkg-config', include_type : 'system')
spdlog_dep = dependency('spdlog', version: '1.9.2')
fmt_dep = dependency('fmt', required: false)
if not fmt_dep.found()
    # If fmt is not found via pkg-config, try to find the library directly
    fmt_dep = meson.get_compiler('cpp').find_library('fmt', required: false)
endif

# Global arrays for unified library build
medialib_include_dirs = [
    include_directories('media_library/include'),
    include_directories('media_library/include/media_library'),
]
medialib_components = []
medialib_dependencies = [
    dsp_dep,
    json_dep,
    expected_dep,
    bsp_thorttling_manager_dep,
    fmt_dep,
    spdlog_dep,
    opencv_dep,
    perfetto_dep,
    rapidjson_dep,
    libiio_dep,
    libhailort_dep,
    freetype_dep,
    harfbuzz_dep,
] + gstreamer_deps
medialib_link_args = ['-lhailo-throttling', '-lcrypto', '-lhantro_vc8000e', '-lm']

subdir('media_library')
subdir('gst')
subdir('api')

# Build unified library after all components are collected
libmedialib = shared_library('medialib',
    link_whole: [git_metadata_lib] + medialib_components,
    include_directories: medialib_include_dirs,
    dependencies: medialib_dependencies,
    link_args: medialib_link_args,
    version: meson.project_version(),
    install: true,
    install_dir: get_option('libdir'),
)

libmedialib_dep = declare_dependency(
    include_directories: medialib_include_dirs,
    dependencies: medialib_dependencies,
    link_with: libmedialib,
)

pkgc.generate(
    libmedialib,
    name: 'hailo-medialib',
    description: 'Hailo Media Library - Unified library for camera frontend, ISP, encoder, and video processing',
    filebase: 'hailo-medialib',
    subdirs: ['hailo'],
)

# Build after libmedialib.so is ready
subdir('gst_plugin')
subdir('executables')
