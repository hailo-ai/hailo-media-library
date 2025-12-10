# Project Declaration
project('media-library', 'c', 'cpp',
    version : '1.9.1',
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
gst_dep = dependency('gstreamer-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_dep'])
gst_base_dep = dependency('gstreamer-base-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_base_dep'])
gst_app_dep = dependency('gstreamer-app-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_app_dep'])
gstvideo_dep = dependency('gstreamer-video-1.0', version : gst_req,
  fallback : ['gst-plugins-base', 'video_dep'])
gstallocators_dep = dependency('gstreamer-allocators-1.0', version : gst_req,
  fallback : ['gst-plugins-base', 'allocators_dep'])

gstreamer_deps = [gst_dep, gst_base_dep, gstvideo_dep, gst_app_dep, gstallocators_dep]
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

if 'core' in targets
    subdir('media_library')
else
    media_library_common_lib = dependency('hailo_media_library_common', method : 'pkg-config', required:false)
    media_library_common_dep = declare_dependency(
        dependencies : media_library_global_lib_deps + [media_library_common_lib],
        include_directories: media_library_global_include_deps)
    media_library_frontend_dep = dependency('hailo_media_library_frontend', method : 'pkg-config', required:false)
    media_library_encoder_dep = dependency('hailo_media_library_encoder', method : 'pkg-config', required:false)
    encoder_dep = dependency('hailo_encoder', method : 'pkg-config', required:false)
    gyro_lib_dep = dependency('hailo_media_library_gyro', method : 'pkg-config', required:false)
endif

if 'gst' in targets
    if 'core' not in targets
        if (not media_library_frontend_dep.found() or
            not media_library_common_dep.found() or
            not encoder_dep.found() or
            not media_library_encoder_dep.found())
                error('Media library core package is missing. please make sure it is located in your system or add core target to the build')
        endif
    endif

    subdir('gst')
else
    gstmedialibrary_utils_dep = dependency('gstmedialibutils', method : 'pkg-config', required:false)
endif


if 'api' in targets
    if 'gst' not in targets
        if (not gstmedialibrary_utils_dep.found())
            error('Gstreamer media library utils package is missing. please make sure it is located in your system or add gst target to the build')
        endif
    endif
    subdir('api')
endif
