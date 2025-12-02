# Project Declaration
project('hailo-analytics', 'c', 'cpp',
    version : '1.9.0',
    default_options : [ 'warning_level=2', 'werror=true',
                        'buildtype=' + get_option('buildtype'),
                        'c_std=c11', 'cpp_std=c++20']
)

# Compiler Arguments
add_global_arguments('-Werror', language : 'cpp')

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

# spdlog Dependencies
spdlog_dep = dependency('spdlog', version: '>= 1.9.2')

# Library Arguments
hailo_lib_args = []

# Apps install dir
apps_install_dir = get_option('apps_install_dir')

# Platform
platform = get_option('platform')

################################################
# Perfetto Dep
################################################
perfetto_dep = meson.get_compiler('cpp').find_library('perfetto', required: false, dirs: '/usr/lib/')

if perfetto_dep.found()
  hailo_lib_args += ['-DHAVE_PERFETTO']
else
  hailo_lib_args += ['-DPERFETTO_NOT_FOUND']
endif

################################################
# MEDIA LIBRARY DEPS
################################################
media_library_common_dep = dependency('hailo_media_library_common', method : 'pkg-config')
media_library_frontend_dep = dependency('hailo_media_library_frontend', method : 'pkg-config')
media_library_encoder_dep = dependency('hailo_media_library_encoder', method : 'pkg-config')
media_library_api_dep = dependency('hailo_media_library_api', method : 'pkg-config')
encoder_dep = dependency('hailo_encoder', method : 'pkg-config')
gstmedialibrary_utils_dep = dependency('gstmedialibutils', method : 'pkg-config')
gyro_lib_dep = dependency('hailo_media_library_gyro', method : 'pkg-config')

apps_dependencies = gst_deps + media_library_encoder_dep + media_library_common_dep + media_library_frontend_dep + encoder_dep + media_library_api_dep + gstmedialibrary_utils_dep + gyro_lib_dep

################################################
# SUBDIRS
################################################
subdir('reference_camera_api')
subdir('apps')