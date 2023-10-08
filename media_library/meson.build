if not dsp_dep.found()
    error('dsp is required')
endif

spdlog_dep = dependency('spdlog', version: '1.9.2')
incdir = [include_directories('./include/media_library')]
utils_incdir = [include_directories('./src/utils')]


common_sourcs = [
    'src/dsp/dsp_utils.cpp',
    'src/buffer_pool/buffer_pool.cpp',
    'src/utils/media_library_logger.cpp',
]

media_library_common_lib = shared_library('hailo_media_library_common',
    common_sourcs,
    cpp_args: common_args,
    include_directories: [incdir, utils_incdir],
    dependencies : [dsp_dep, spdlog_dep],
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

dis_incdir = [include_directories('./src/dis_library')]
dis_sources = [
  'src/dis_library/dis.cpp',
  'src/dis_library/dis_interface.cpp',
]

dis_library_lib = shared_library('dis_library',
    dis_sources,
    cpp_args: common_args,
    include_directories: [dis_incdir],
    dependencies : [],
    version: meson.project_version(),
    install: true,
    install_dir: get_option('libdir'),
)

dis_library_dep = declare_dependency(
  include_directories: [include_directories('.')],
  link_with : dis_library_lib)

frontend_sources = [
    'src/vision_pre_proc/vision_pre_proc.cpp',
    'src/config_manager/config_manager.cpp',
    'src/vision_pre_proc/generate_mesh.cpp'
]

media_library_frontend_lib = shared_library('hailo_media_library_frontend',
    frontend_sources,
    cpp_args: common_args,
    include_directories: [incdir, dis_incdir, utils_incdir],
    dependencies : [dsp_dep, dis_library_dep, spdlog_dep, json_dep, expected_dep, media_library_common_dep],
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
    'src/hailo_encoder/encoder_schema.cpp',
    'src/hailo_encoder/encoder_gop_config.cpp',
]

hailo_media_library_encoder_lib = shared_library('hailo_media_library_encoder',
    media_library_encoder_lib_sources,
    cpp_args: common_args,
    link_args: ['-lhantro_vc8000e', '-lm'],
    dependencies : [media_library_common_dep, spdlog_dep, rapidjson_dep],
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