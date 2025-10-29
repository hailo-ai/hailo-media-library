if not dsp_dep.found()
    error('dsp is required')
endif

incdir = [include_directories('./include/media_library')]
utils_incdir = [include_directories('./src/utils')]
denoise_incdir = [include_directories('./src/front_end/denoise')]
sensor_registry_incdir = [include_directories('./src/isp/sensor_registry')]

platform = get_option('platform') 


common_sources = [
    'src/utils/files_utils.cpp',
    'src/dsp/dsp_utils.cpp',
    'src/isp/isp_utils.cpp',
    'src/isp/v4l2_ctrl.cpp',
    'src/isp/dma_buffer/dma_buffer.cpp',
    'src/isp/video_buffer/video_buffer.cpp',
    'src/isp/video_device/video_device.cpp',
    'src/isp/sensor_registry/sensor_registry.cpp',
    'src/isp/sensor_registry/sensor_types.cpp',
    'src/buffer_pool/buffer_pool.cpp',
    'src/buffer_pool/dma_memory_allocator.cpp',
    'src/utils/media_library_logger.cpp',
    'src/state_monitor/throttling_state_monitor.cpp',
    'src/utils/signal_utils.cpp',
    'src/utils/threadpool.cpp',
    'src/snapshot/snapshot.cpp',
    'src/utils/pipe_handler.cpp',
    'src/analytics_db/analytics_db.cpp',
    'src/privacy_mask/privacy_mask.cpp',
    'src/privacy_mask/polygon_math.cpp'
]
if perfetto_dep.found()
  common_sources += ['src/perfetto/perfetto.cpp']
endif
subdir('./src/config_manager')

media_library_common_lib = shared_library('hailo_media_library_common',
    common_sources,
    cpp_args: common_args,
    include_directories: [incdir, utils_incdir] + media_library_global_include_deps,
    dependencies : [dsp_dep, json_dep, expected_dep, bsp_thorttling_manager_dep, fmt_dep] + media_library_global_lib_deps,
    link_whole: git_metadata_lib,
    version: meson.project_version(),
    install: true,
    install_dir: get_option('libdir'),
    link_args: ['-lhailo-throttling'],
)

media_library_common_dep = declare_dependency(
  include_directories: [incdir, include_directories('./include')] + media_library_global_include_deps,
  dependencies : media_library_global_lib_deps,
  link_with : media_library_common_lib)

pkgc.generate(name: 'hailo_media_library_common',
              libraries: media_library_common_lib,
              subdirs: 'hailo',
              version: meson.project_version(),
              description: 'Hailo Media Library Common',
)

subdir('./src/dis_library')
subdir('./src/eis_library')

frontend_sources = [
    'src/front_end/multi_resize.cpp',
    'src/front_end/dewarp.cpp',
    'src/front_end/ldc_mesh_context.cpp',
    'src/front_end/denoise/hailort_denoise.cpp',
    'src/front_end/denoise/denoise.cpp',
    'src/front_end/denoise/pre_isp_denoise.cpp',
    'src/front_end/denoise/post_isp_denoise.cpp',
    'src/front_end/motion_detection.cpp',
    'src/front_end/dsp_image_enhancement.cpp'
]

subdir('./src/hdr')

libiio_dep = dependency('libiio', version: '>=0.19', required: true)

gyro_sources = [
  'src/eis_library/gyro_device.cpp',
  'src/eis_library/arguments_parser.cpp'
]

gyro_lib = shared_library('gyro_library',
    gyro_sources,
    cpp_args: common_args,
    include_directories: [eis_incdir, incdir],
    dependencies : [libiio_dep, media_library_common_dep],
    version: meson.project_version(),
    install: true,
    install_dir: get_option('libdir'),
)

gyro_lib_dep = declare_dependency(
  include_directories: [include_directories('./include')],
  link_with : gyro_lib)

pkgc.generate(name: 'hailo_media_library_gyro',
              libraries: gyro_lib,
              subdirs: 'hailo',
              version: meson.project_version(),
              description: 'Hailo Media Library Gyro',
)

executable('gyro_calibration',
  gyro_sources,
  include_directories: [eis_incdir, incdir],
  dependencies : [libiio_dep, media_library_common_dep],
  link_whole: git_metadata_lib,
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: get_option('bindir'),
)


snapshot_cli_sources = ['src/snapshot/snapshot_cli.cpp']
executable('snapshot_cli',
  snapshot_cli_sources,
  include_directories: [incdir],
  dependencies : [media_library_common_dep],
  link_whole: git_metadata_lib,
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: get_option('bindir'),
)


media_library_frontend_lib = shared_library('hailo_media_library_frontend',
    frontend_sources,
    cpp_args: common_args,
    include_directories: [incdir, dis_incdir, eis_incdir, utils_incdir, denoise_incdir, hdr_incdir, sensor_registry_incdir],
    dependencies : [opencv_dep, dsp_dep, dis_library_dep, gyro_lib_dep, spdlog_dep, json_dep, expected_dep, media_library_common_dep, libhailort_dep, eis_lib_dep, fmt_dep],
    link_whole: git_metadata_lib,
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
    link_whole: git_metadata_lib,
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
    'src/hailo_encoder/encoder_config_presets.cpp',
    'src/hailo_encoder/encoder_gop_config.cpp',
]

hailo_media_library_encoder_lib = shared_library('hailo_media_library_encoder',
    media_library_encoder_lib_sources,
    cpp_args: common_args,
    link_args: ['-lhantro_vc8000e', '-lm'],
    dependencies : [media_library_common_dep, spdlog_dep, fmt_dep],
    link_whole: git_metadata_lib,
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
