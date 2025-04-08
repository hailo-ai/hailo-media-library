eis_incdir = [include_directories('./')]

eis_sources = [
  'eis.cpp',
]

eis_lib = shared_library('eis_library',
    eis_sources,
    cpp_args: common_args,
    include_directories: [eis_incdir, incdir, utils_incdir],
    dependencies : [opencv_dep, json_dep, spdlog_dep, media_library_common_dep],
    version: meson.project_version(),
    install: true,
    install_dir: get_option('libdir'),
)

eis_lib_dep = declare_dependency(
  include_directories: [include_directories('.')],
  dependencies : [],
  link_with : eis_lib)
