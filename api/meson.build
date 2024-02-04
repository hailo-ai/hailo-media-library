################################################
# APP WRAPPER SOURCE
################################################
sources = [
  'src/encoder/encoder.cpp',
  'src/frontend/frontend.cpp'
  ]

json_dep = meson.get_compiler('c').find_library('libnlohmann_json_schema_validator', required: true, dirs: '/usr/lib/')

encoder_lib = shared_library('hailo_medialibrary_api',
  sources,
  include_directories: include_directories('./include/'),
  dependencies : [gstreamer_deps, json_dep, gstmedialibrary_utils_dep, media_library_common_dep],
  version: meson.project_version(),
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: get_option('libdir'),
)

encoder_dep = declare_dependency(
  include_directories: [include_directories('./include/')],
  dependencies : [media_library_common_dep],
  link_with : encoder_lib)

################################################
# VisionPreProc Example
################################################
vision_preproc_example_src = ['examples/vision_preproc_example.cpp']

executable('vision_preproc_example',
  vision_preproc_example_src,
  dependencies : gstreamer_deps + [media_library_frontend_dep, gstmedialibrary_utils_dep, encoder_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: get_option('bindir'),
)

################################################
# Frontend Example
################################################
frontend_example_src = ['examples/frontend_example.cpp']

executable('frontend_example',
  frontend_example_src,
  dependencies : gstreamer_deps + [media_library_frontend_dep, gstmedialibrary_utils_dep, encoder_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: get_option('bindir'),
)

polygon_example_src = ['examples/polygon_example.cpp']

executable('polygon_example',
  polygon_example_src,
  dependencies : gstreamer_deps + [media_library_frontend_dep, gstmedialibrary_utils_dep, encoder_dep, opencv_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: get_option('bindir'),
)

install_subdir('include/media_library', install_dir: get_option('includedir') + '/hailo')