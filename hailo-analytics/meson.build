# Project Declaration
project('hailo-analytics', 'c', 'cpp',
    version : '8.8.8',
    default_options : [ 'warning_level=2', 'werror=true',
                        'buildtype=' + get_option('buildtype'),
                        'c_std=c11', 'cpp_std=c++20']
)

# Compiler Arguments
compiler = meson.get_compiler('cpp')

# Pkgconfig
pkgc = import('pkgconfig')

# Docs building
build_docs = get_option('build_docs')
if build_docs
    subdir('docs')
    subdir_done()
endif

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

# Logger Dependencies
spdlog_dep = dependency('spdlog', version: '>= 1.9.2')
fmt_dep = dependency('fmt', required: false)

# HailoRT Dependencies
libhailort_dep = [meson.get_compiler('cpp').find_library('hailort', required: false)]

# Library Arguments
hailo_lib_args = []

# Platform
platform = get_option('platform')

################################################
# Perfetto Dep
################################################
if get_option('perfetto')
    perfetto_dep = meson.get_compiler('cpp').find_library('perfetto', required: false, dirs: '/usr/lib/')
    if perfetto_dep.found()
        hailo_lib_args += ['-DHAVE_PERFETTO']
    else
        perfetto_dep = dependency('', required: false)
        hailo_lib_args += ['-DPERFETTO_NOT_FOUND']
    endif
else
    perfetto_dep = dependency('', required: false)
    hailo_lib_args += ['-DPERFETTO_NOT_FOUND']
endif

################################################
# MEDIA LIBRARY DEPS
################################################
postprocess_tools_dep = dependency('hailo-postprocess-tools', method : 'pkg-config', include_type: 'system')
medialib_dep = dependency('hailo-medialib', method : 'pkg-config')

base_deps = gst_deps + medialib_dep + postprocess_tools_dep + libhailort_dep + perfetto_dep + spdlog_dep + fmt_dep

################################################
# SUBDIRS
################################################
subdir('hailo_analytics_api')
subdir('apps')

if get_option('build_verification')
    subdir('verification')
endif
