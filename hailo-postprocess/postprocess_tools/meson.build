# api includes
include_path = 'include'
incdir = [include_directories(include_path)]
install_subdir(include_path + '/hailo_postprocess_tools', install_dir : get_option('includedir') + '/hailo')

hailo_postprocess_tools_components = []

################################################
# SUBDIRS
################################################
subdir('src/image_utils')
subdir('src/objects')
subdir('src/tracking')

################################################
# UNIFIED LIB
################################################
lib_hailo_postprocess_tools = shared_library('hailo_postprocess_tools',
    link_whole: hailo_postprocess_tools_components,
    include_directories: incdir,
    dependencies: base_deps,
    version: meson.project_version(),
    install: true,
    install_dir: get_option('libdir'),
)
libhailo_postprocess_tools_dep = declare_dependency(
    include_directories: incdir,
    dependencies: base_deps,
    link_with: lib_hailo_postprocess_tools,
)
pkgc.generate(
    lib_hailo_postprocess_tools,
    name: 'hailo-postprocess-tools',
    description: 'Hailo Postprocess Tools - Unified library for Hailo postprocessing tools',
    filebase: 'hailo-postprocess-tools',
    requires: base_deps,
    subdirs: ['hailo'],
)