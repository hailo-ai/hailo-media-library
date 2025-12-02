################################################
# APP WRAPPER SOURCE
################################################
sources = [
  'src/encoder/encoder.cpp',
  'src/frontend/frontend.cpp',
  'src/media_library/media_library.cpp',
  'src/media_library/media_library_api_types.cpp',
  'src/utils/utils.cpp'
  ]

api_include_dirs = [include_directories('./include/')]
medialib_include_dirs += api_include_dirs

libapi_wrapper = static_library('api_wrapper_internal',
    sources,
    cpp_args: common_args,
    include_directories: medialib_include_dirs,
    dependencies: medialib_dependencies,
    pic: true,
)

medialib_components += [libapi_wrapper]

install_subdir('include/media_library', install_dir: get_option('includedir') + '/hailo')
install_subdir('examples/config_examples', install_dir: get_option('bindir'), strip_directory: true)
