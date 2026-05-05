# Project Declaration
project('hailo-postprocess', 'c', 'cpp',
    version : '1.11.0',
    default_options : [ 'buildtype=' + get_option('buildtype'),
                        'c_std=c11', 'cpp_std=c++17']
)

# Compiler Arguments
add_global_arguments('-Wpessimizing-move', language : 'cpp')
add_global_arguments('-Wredundant-move', language : 'cpp')
add_global_arguments('-fconcepts', language : 'cpp')

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

# Library Arguments
hailo_lib_args = []

base_deps = gst_deps + [opencv_dep, xtensor_dep]

################################################
# SUBDIRS
################################################
subdir('postprocess_tools')
subdir('postprocesses')