if not dsp_dep.found()
    error('dsp is required')
endif

incdir = [include_directories('./include/media_library')]
utils_incdir = [include_directories('./src/utils')]
denoise_incdir = [include_directories('./src/front_end/denoise')]
sensor_registry_incdir = [include_directories('./src/isp/sensor_registry')]

platform = get_option('platform')

subdir('./src/config_parser')

isp_source_files = files(
    'src/isp/isp_utils.cpp',
    'src/isp/isp_manager.cpp',
    'src/isp/v4l2_ctrl.cpp',
    'src/isp/dma_buffer/dma_buffer.cpp',
    'src/isp/video_buffer/video_buffer.cpp',
    'src/isp/video_device/video_device.cpp',
    'src/isp/sensor_registry/sensor_registry.cpp',
    'src/isp/sensor_registry/sensor_types.cpp',
)
common_sources_files = files(
    'src/utils/files_utils.cpp',
    'src/utils/media_library_types.cpp',
    'src/osd/osd_types.cpp',
    'src/dsp/dsp_utils.cpp',
    'src/buffer_pool/buffer_pool.cpp',
    'src/buffer_pool/dma_memory_allocator.cpp',
    'src/utils/media_library_logger.cpp',
    'src/state_monitor/throttling_state_monitor.cpp',
    'src/utils/signal_utils.cpp',
    'src/utils/threadpool.cpp',
    'src/snapshot/snapshot.cpp',
    'src/snapshot/snapshot_buffer_io.cpp',
    'src/snapshot/snapshot_commands.cpp',
    'src/snapshot/snapshot_utils.cpp',
    'src/config_manager/config_manager.cpp',
    'src/config_manager/config_backup.cpp',
    'src/utils/pipe_handler.cpp',
    'src/analytics_db/analytics_db.cpp',
    'src/config_attacher/config_attacher.cpp',
    'src/privacy_mask/privacy_mask_blender.cpp',
    'src/privacy_mask/privacy_mask_blender_db.cpp',
    'src/privacy_mask/privacy_mask_configurer.cpp',
    'src/privacy_mask/privacy_mask_dynamic.cpp',
    'src/privacy_mask/polygon_math.cpp',
    'src/trace/fps_tracer.cpp',
    'src/trace/perfetto_fps_tracer.cpp'
)

common_sources_files += isp_source_files

if perfetto_dep.found()
  common_sources_files += files('src/trace/perfetto.cpp')
endif

medialib_include_dirs += [utils_incdir, sensor_registry_incdir, include_directories('./src/trace')]

libcommon = static_library('common_internal',
    common_sources_files,
    cpp_args: common_args,
    include_directories: medialib_include_dirs,
    dependencies: medialib_dependencies,
    pic: true,
)

medialib_components += [libcommon]

subdir('./src/dis_library')
subdir('./src/eis_library')

frontend_sources = [
    'src/front_end/multi_resize.cpp',
    'src/front_end/dewarp.cpp',
    'src/front_end/ldc_mesh_context.cpp',
    'src/front_end/motion_detection.cpp',
    'src/front_end/dsp_image_enhancement.cpp'
]

subdir('./src/hdr')
subdir('./src/front_end/denoise')

libfrontend = static_library('frontend_internal',
    frontend_sources,
    cpp_args: common_args,
    include_directories: medialib_include_dirs,
    dependencies: medialib_dependencies,
    pic: true,
)
medialib_components += [libfrontend]

media_library_encoder_lib_sources = [
    'src/hailo_encoder/encoder.cpp',
    'src/hailo_encoder/hailo_encoder_impl.cpp',
    'src/hailo_encoder/encoder_config.cpp',
    'src/hailo_encoder/encoder_gop_config.cpp',
]

libmedialib_encoder = static_library('medialib_encoder_impl',
    media_library_encoder_lib_sources,
    cpp_args: common_args,
    dependencies: medialib_dependencies,
    include_directories: medialib_include_dirs,
    pic: true,
)
medialib_components += [libmedialib_encoder]

install_data('src/hailo_encoder/encoder_presets.csv', install_dir: '/etc/medialib')
install_subdir('include/media_library', install_dir: get_option('includedir') + '/hailo')
