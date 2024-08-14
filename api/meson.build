################################################
# APP WRAPPER SOURCE
################################################
sources = [
  'src/encoder/encoder.cpp',
  'src/frontend/frontend.cpp'
  ]

json_dep = meson.get_compiler('c').find_library('libnlohmann_json_schema_validator', required: true, dirs: '/usr/lib/')

media_library_api_lib = shared_library('hailo_media_library_api',
  sources,
  include_directories: include_directories('./include/'),
  dependencies : [gstreamer_deps, json_dep, gstmedialibrary_utils_dep, spdlog_dep, media_library_common_dep],
  version: meson.project_version(),
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: get_option('libdir'),
)

media_library_api_dep = declare_dependency(
  include_directories: [include_directories('./include/')],
  dependencies : [media_library_common_dep],
  link_with : media_library_api_lib)

pkgc.generate(name: 'hailo_media_library_api',
              libraries: media_library_api_lib,
              subdirs: 'hailo',
              version: meson.project_version(),
              description: 'Hailo Media Library API',
)

################################################
# Frontend Example
################################################
frontend_example_src = ['examples/frontend_example.cpp']

executable('frontend_example',
  frontend_example_src,
  dependencies : gstreamer_deps + [media_library_frontend_dep, gstmedialibrary_utils_dep, media_library_api_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: get_option('bindir'),
)

executable('jpeg_frontend_example',
  frontend_example_src,
  cpp_args: ['-DUSE_JPEG_JSONS'],
  dependencies : gstreamer_deps + [media_library_frontend_dep, gstmedialibrary_utils_dep, media_library_api_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: get_option('bindir'),
)

################################################
# Encoder Example
################################################
encoder_example_src = ['examples/encoder_example.cpp']
executable('encoder_example',
    encoder_example_src,
    dependencies : gstreamer_deps + [media_library_frontend_dep, gstmedialibrary_utils_dep, media_library_api_dep],
    gnu_symbol_visibility : 'default',
	  install: true,
    install_dir: get_option('bindir'),
)



################################################
# Misc
################################################

polygon_example_src = ['examples/polygon_example.cpp']

executable('polygon_example',
  polygon_example_src,
  dependencies : gstreamer_deps + [media_library_frontend_dep, gstmedialibrary_utils_dep, media_library_api_dep, opencv_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: get_option('bindir'),
)

calculate_size_osd_src = ['examples/calculate_text_size_osd.cpp']

executable('calc_osd_text_size',
  calculate_size_osd_src,
  dependencies : [opencv_dep, media_library_api_dep, gstmedialibrary_utils_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: get_option('bindir'),
)

install_subdir('include/media_library', install_dir: get_option('includedir') + '/hailo')