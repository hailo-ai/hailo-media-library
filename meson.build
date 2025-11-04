# Project Declaration
project('media-library', 'c', 'cpp',
    version : '1.8.0',
    default_options : [ 'warning_level=2', 'werror=true',
                        'buildtype=' + get_option('buildtype'),
                        'c_std=c11', 'cpp_std=c++20']
)

# Compiler Arguments
add_global_arguments('-Werror', language : 'cpp')

pkgc = import('pkgconfig')
targets = get_option('targets')

if 'docs' in targets
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
rapidjson_dep = dependency('RapidJSON', method : 'pkg-config')
json_dep = meson.get_compiler('cpp').find_library('libnlohmann_json_schema_validator', required: true, dirs: '/usr/lib/')
opencv_dep = dependency('opencv4', version : '>= 4.0', method : 'pkg-config', include_type : 'system')
spdlog_dep = dependency('spdlog', version: '1.9.2')
fmt_dep = dependency('fmt', required: false)
# If fmt is not found via pkg-config, try to find the library directly
if not fmt_dep.found()
    fmt_dep = meson.get_compiler('cpp').find_library('fmt', required: false)
endif
media_library_global_lib_deps = [spdlog_dep, opencv_dep, perfetto_dep]
media_library_global_include_deps = [include_directories('./media_library/src/perfetto/')]

subdir('media_library')
subdir('gst')
subdir('api')
