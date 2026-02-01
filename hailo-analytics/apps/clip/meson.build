################################################
# HAILO 15 AI EXAMPLE APP
################################################

# Dependencies
libsql_link = compiler.find_library('libsqlite3', required: true, dirs: '/usr/lib')
libfaiss_link = compiler.find_library('libfaiss', required: true, dirs: '/usr/lib')
libdatachannel_link = compiler.find_library('libdatachannel', required : true, dirs : '/usr/lib')

libavcodec_link = compiler.find_library('libavcodec', required: true, dirs: '/usr/lib')
libavformat_link = compiler.find_library('libavformat', required: true, dirs: '/usr/lib')
libavutil_link = compiler.find_library('libavutil', required: true, dirs: '/usr/lib')

libtokenizers_cpp = compiler.find_library('tokenizers_cpp', dirs: '/usr/lib')
libtokenizers_c = compiler.find_library('tokenizers_c', dirs: '/usr/lib')

tokenizers_dep = declare_dependency(
  dependencies: [
    libtokenizers_cpp,    # C++ wrapper library
    libtokenizers_c       # Rust core library (despite the name)
  ],
  version: '1.0.0'
)

clip_install_dir = apps_install_dir + '/clip'

app_src = [
  'apps/webserver.cpp'
]

database_src = [
  'database/src/database.cpp',
  'database/src/faiss_table.cpp',
  'database/src/thumbnail_table.cpp',
  'database/src/video_table.cpp'
]

custom_pipeline_src = [
  'custom/converter/segmented_mkv_muxer.cpp',
  'custom/streaming/mkv_streamer.cpp',
  'custom/service/player_service_ext.cpp',
  'custom/service/storage_cleanup_service_ext.cpp',
  'utils/common_utils.cpp',
  'utils/clip_app_config_parser.cpp'
]

# Include tokenizers subdirectory to build the sample code for test if needed
# subdir('utils/tokenizers')

# Create a library for clip components (without webserver and main)
clip_lib_src = database_src + custom_pipeline_src

clip_include_dirs = include_directories('utils', 'database', 'database/include', 'custom', 'apps', 'custom/converter')

clip_lib = static_library('clip',
  clip_lib_src,
  cpp_args : hailo_lib_args,
  dependencies : [tokenizers_dep, libavcodec_link, libavformat_link, libavutil_link, libdatachannel_link, libfaiss_link, libsql_link, libhailo_analytics_dep],
  gnu_symbol_visibility : 'default',
  include_directories: clip_include_dirs,
  install: false,
)

# Declare the dependency for external use
clip_dep = declare_dependency(
  link_with: clip_lib,
  dependencies: [tokenizers_dep, libavcodec_link, libavformat_link, libavutil_link, libdatachannel_link, libfaiss_link, libsql_link, libhailo_analytics_dep],
  include_directories: clip_include_dirs,
)

# Build the main executable using the library
clip_app_src = app_src + ['main.cpp']

executable('clip_app',
  clip_app_src,
  cpp_args : hailo_lib_args,
  dependencies : [clip_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: clip_install_dir,
  include_directories: ['utils'],
)

# Install config files to /resources/configs/
install_data(
  sources: ['resources/configs/clip_app_config.yaml',
            'resources/configs/yolov8n_personface.json'],
  install_dir: clip_install_dir / 'resources/configs'
)

# Install webfrontend files to /resources/webfrontend/
install_data(
  sources: ['resources/webfrontend/index.html'],
  install_dir: clip_install_dir / 'resources/webfrontend'
)
