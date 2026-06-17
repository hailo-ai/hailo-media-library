# Project Declaration
project('hailo-postprocess', 'c', 'cpp',
    version : '1.12.0',
    default_options : [ 'warning_level=2', 'werror=true',
                        'buildtype=' + get_option('buildtype'),
                        'c_std=c11', 'cpp_std=c++20']
)

# Compiler Arguments
add_global_arguments('-fconcepts', language : 'cpp')
# Suppress deprecated-enum-enum-conversion warnings from OpenCV 4.x headers (fixed in OpenCV 4.8+)
add_global_arguments('-Wno-deprecated-enum-enum-conversion', language : 'cpp')

# Pkgconfig
pkgc = import('pkgconfig')

# Gstreamer Dependencies
gst_req = '>= 1.0.0'
gst_dep = dependency('gstreamer-1.0', required: true, version : gst_req)
gst_base_dep = dependency('gstreamer-base-1.0', required: true, version : gst_req)
gst_app_dep = dependency('gstreamer-app-1.0', required: true, version : gst_req)
gstvideo_dep = dependency('gstreamer-video-1.0', required: true, version : gst_req)
gstallocators_dep = dependency('gstreamer-allocators-1.0', required: true, version : gst_req)
gst_deps = [gst_dep, gst_base_dep, gstvideo_dep, gst_app_dep, gstallocators_dep]

# Opencv Dependencies
opencv_dep = dependency('opencv4', version : '>= 4.0', method : 'pkg-config')

# xtensor Dependencies
xtensor_dep = dependency('xtensor', method : 'pkg-config')

# Logger Dependencies
spdlog_dep = dependency('spdlog', version: '>= 1.9.2')
fmt_dep = dependency('fmt', required: false)

# Media Library Dependency (for logger setup)
medialib_dep = dependency('hailo-medialib', method : 'pkg-config')

# Library Arguments
hailo_lib_args = []

base_deps = gst_deps + [opencv_dep, xtensor_dep, spdlog_dep, fmt_dep, medialib_dep]

################################################
# SUBDIRS
################################################
subdir('postprocess_tools')
subdir('postprocesses')