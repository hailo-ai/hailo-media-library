cpp = meson.get_compiler('cpp')

# Add your source files here
sources = ['main.cpp', 
            'resources/encoder.cpp',
            'resources/osd.cpp',
            'resources/frontend.cpp',
            'resources/ai.cpp',
            'resources/repository.cpp',
            'resources/isp.cpp',
            'resources/privacy_mask.cpp',
            'pipeline/pipeline.cpp',
            'pipeline/dummy_pipeline.cpp',
            'pipeline/pipeline_interface.cpp',
            'common/isp/v4l2_ctrl.cpp',
            'common/isp/common.cpp'
]

# Add any additional dependencies here
dependencies = [dependency('threads'), dependency('glib-2.0'), gstreamer_deps, media_library_frontend_dep]

executable('medialib-webserver',
  sources,
  dependencies: dependencies,
  gnu_symbol_visibility : 'default',
  include_directories: [include_directories('../media_library/include/media_library')],
  cpp_args: common_args,
  install: true,
  install_dir: get_option('bindir'),
)