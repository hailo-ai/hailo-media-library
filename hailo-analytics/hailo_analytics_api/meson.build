# api includes
include_path = 'include'
incdir = [include_directories(include_path)]
install_subdir(include_path + '/hailo_analytics', install_dir : get_option('includedir') + '/hailo')

hailo_analytics_components = []

################################################
# SUBDIRS
################################################
if perfetto_dep.found()
    subdir('src/perfetto')
endif
subdir('src/logger')
subdir('src/utils')
subdir('src/pipeline/core')
subdir('src/pipeline/sources')
subdir('src/pipeline/sinks')
subdir('src/pipeline/codecs')
subdir('src/pipeline/ai')
subdir('src/pipeline/overlay')
subdir('src/pipeline/muxing')
subdir('src/pipeline/routing')
subdir('src/pipeline/cropping')
subdir('src/analytics')

################################################
# UNIFIED LIB
################################################
lib_hailo_analytics = shared_library('hailo_analytics',
    link_whole: hailo_analytics_components,
    include_directories: incdir,
    dependencies: base_deps,
    version: meson.project_version(),
    install: true,
    install_dir: get_option('libdir'),
)
libhailo_analytics_dep = declare_dependency(
    include_directories: incdir,
    dependencies: base_deps,
    link_with: lib_hailo_analytics,
)
pkgc.generate(
    lib_hailo_analytics,
    name: 'hailo-analytics',
    description: 'Hailo Analytics - Unified library for Hailo Analytics libraries',
    filebase: 'hailo-analytics',
    subdirs: ['hailo'],
)