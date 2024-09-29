cpp = meson.get_compiler('cpp')

# Add libdatachannel manually
libdatachannel_link = cpp.find_library('libdatachannel', required : true, dirs : '/usr/lib')

# Add your source files here
sources = ['main.cpp', 
            'resources/encoder.cpp',
            'resources/osd.cpp',
            'resources/configs.cpp',
            'resources/frontend.cpp',
            'resources/ai.cpp',
            'resources/repository.cpp',
            'resources/isp.cpp',
            'resources/privacy_mask.cpp',
            'resources/webpage.cpp',
            'resources/webrtc.cpp',
            'pipeline/pipeline.cpp',
            'pipeline/dummy_pipeline.cpp',
            'pipeline/pipeline_interface.cpp',
            'common/isp/common.cpp',
            'common/httplib/httplib_utils.cpp',
            'common/webserver_logger.cpp'
]

# Add any additional dependencies here
dependencies = [dependency('threads'), dependency('glib-2.0'), gstreamer_deps, media_library_frontend_dep, media_library_common_dep, gyro_lib_dep, spdlog_dep, libdatachannel_link]

executable('medialib-webserver',
  sources,
  dependencies: dependencies,
  gnu_symbol_visibility : 'default',
  include_directories: [include_directories('../media_library/include')],
  cpp_args: common_args,
  install: true,
  install_dir: get_option('bindir'),
)