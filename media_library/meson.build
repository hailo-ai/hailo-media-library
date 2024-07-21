if not dsp_dep.found()
    error('dsp is required')
endif

incdir = [include_directories('./include/media_library')]
utils_incdir = [include_directories('./src/utils')]


common_sourcs = [
    'src/dsp/dsp_utils.cpp',
    'src/isp/isp_utils.cpp',
    'src/buffer_pool/buffer_pool.cpp',
    'src/buffer_pool/dma_memory_allocator.cpp',
    'src/utils/media_library_logger.cpp',
    'src/utils/signal_utils.cpp',
    'src/utils/threadpool.cpp',
    'src/config_manager/config_manager.cpp'
]

media_library_common_lib = shared_library('hailo_media_library_common',
    common_sourcs,
    cpp_args: common_args,
    include_directories: [incdir, utils_incdir],
    dependencies : [opencv_dep, dsp_dep, spdlog_dep, json_dep, expected_dep],
    version: meson.project_version(),
    install: true,
    install_dir: get_option('libdir'),
)

media_library_common_dep = declare_dependency(
  include_directories: [include_directories('./include')],
  dependencies : [spdlog_dep],
  link_with : media_library_common_lib)

pkgc.generate(name: 'hailo_media_library_common',
              libraries: media_library_common_lib,
              subdirs: 'hailo',
              version: meson.project_version(),
              description: 'Hailo Media Library Common',
)

subdir('./src/dis_library')

frontend_sources = [
    'src/vision_pre_proc/vision_pre_proc.cpp',
    'src/vision_pre_proc/dewarp_mesh_context.cpp',
    'src/front_end/multi_resize.cpp',
    'src/front_end/dewarp.cpp',
    'src/front_end/ldc_mesh_context.cpp',
    'src/front_end/privacy_mask.cpp',
    'src/front_end/polygon_math.cpp',
    'src/front_end/denoise.cpp',
    'src/front_end/defog.cpp'
]

if get_option('hailort_4_16')
    message('Building media_library with Hailort 4.16')
    frontend_sources += [
    ]
    common_args += ['-DHAILORT_4_16']
endif

media_library_frontend_lib = shared_library('hailo_media_library_frontend',
    frontend_sources,
    cpp_args: common_args,
    include_directories: [incdir, dis_incdir, utils_incdir],
    dependencies : [opencv_dep,  dsp_dep, dis_library_dep, spdlog_dep, json_dep, expected_dep, media_library_common_dep, libhailort_dep],
    version: meson.project_version(),
    install: true,
    install_dir: get_option('libdir'),
)

media_library_frontend_dep = declare_dependency(
  include_directories: [include_directories('./include')],
  link_with : media_library_frontend_lib)

pkgc.generate(name: 'hailo_media_library_frontend',
              libraries: media_library_frontend_lib,
              subdirs: 'hailo',
              version: meson.project_version(),
              description: 'Hailo Media Library Frontend',
)

encoder_lib_sources = [
    'src/encoder/gop_config.cpp',
    'src/encoder/hailo_encoder.cpp',
]

encoder_lib = shared_library('hailo_encoder',
    encoder_lib_sources,
    cpp_args: common_args,
    link_args: ['-lhantro_vc8000e', '-lm'],
    include_directories: [incdir],
    dependencies : [media_library_common_dep],
    version: meson.project_version(),
    install: true,
    install_dir: get_option('libdir'),
)

encoder_dep = declare_dependency(
  include_directories: [include_directories('./include')],
  link_args : ['-lhantro_vc8000e', '-lm'],
  link_with : encoder_lib)

pkgc.generate(name: 'hailo_encoder',
              libraries: encoder_lib,
              subdirs: 'hailo',
              version: meson.project_version(),
              description: 'Hailo Encoder',
)

media_library_encoder_lib_sources = [
    'src/hailo_encoder/encoder.cpp',
    'src/hailo_encoder/encoder_config.cpp',
    'src/hailo_encoder/encoder_gop_config.cpp',
]

hailo_media_library_encoder_lib = shared_library('hailo_media_library_encoder',
    media_library_encoder_lib_sources,
    cpp_args: common_args,
    link_args: ['-lhantro_vc8000e', '-lm'],
    dependencies : [media_library_common_dep, spdlog_dep],
    include_directories: [incdir, utils_incdir],
    version: meson.project_version(),
    install: true,
    install_dir: get_option('libdir'),
)

media_library_encoder_dep = declare_dependency(
  include_directories: [include_directories('./include')],
  link_with : hailo_media_library_encoder_lib)

pkgc.generate(name: 'hailo_media_library_encoder',
              libraries: hailo_media_library_encoder_lib,
              subdirs: 'hailo',
              version: meson.project_version(),
              description: 'Hailo Media Library Encoder',
)

install_subdir('include/media_library', install_dir: get_option('includedir') + '/hailo')
