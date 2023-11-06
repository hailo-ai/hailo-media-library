dis_incdir = [include_directories('./')]

dis_sources = [
  'dis.cpp',
  'dis_interface.cpp',
]

dis_library_lib = shared_library('dis_library',
    dis_sources,
    cpp_args: common_args,
    include_directories: [dis_incdir, incdir],
    dependencies : [],
    version: meson.project_version(),
    install: true,
    install_dir: get_option('libdir'),
)

dis_library_dep = declare_dependency(
  include_directories: [include_directories('.')],
  link_with : dis_library_lib)