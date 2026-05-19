unit_tests_install_dir = tests_install_dir + '/unit_tests'

cpp_unit_tests_sources = [
  'test_main.cpp',
  'core_tests/core_tests_common.cpp',
  'core_tests/buffer_tests.cpp',
  'core_tests/queue_tests.cpp',
  'core_tests/stage_tests.cpp',
  'core_tests/pipeline_tests.cpp',
  'core_tests/bundling_tests.cpp',
  'sink_tests/udp_stage_tests.cpp',
  'codec_tests/encoder_stage_tests.cpp',
  'codec_tests/encoder_stage_dpm_conversion_tests.cpp',
  'analytics_tests/test_images/test_image_loader.cpp',
  'analytics_tests/test_image_loader_tests.cpp',
  'analytics_tests/ocr_pipeline_tests.cpp',
  'codec_tests/analytic_metadata_proto_tests.cpp',
  'sink_tests/metadata_ws_sink_stage_tests.cpp',
  'ai_tests/detection_tracker_stage_tests.cpp',
  'ai_tests/tiling_tracker_config_tests.cpp',
  'ai_tests/dpm_runtime_tests.cpp',
  'ai_tests/bbox_crop_stage_tests.cpp'
]

# Only include encoder and ocr tests on 15h
if platform == '15h'
  cpp_unit_tests_sources += 'source_tests/frontend_stage_tests.cpp'
endif

media_library_examples_inc = include_directories('../../../hailo-media-library/api/examples')
media_library_test_inc = include_directories('../../../verification/unit_tests')
test_images_inc = include_directories('analytics_tests/test_images')

test_images_install_dir = unit_tests_install_dir + '/test_images'

platform_define = (platform == '15h') ? '-DHAILO_PLATFORM_15H' : '-DHAILO_PLATFORM_15L'

# libdatachannel needed for WS sink test client (rtc::WebSocket)
libdatachannel_dep = compiler.find_library('libdatachannel', required : true, dirs : '/usr/lib')

executable('hailo_analytics_unit_tests',
  cpp_unit_tests_sources,
  include_directories: incdir + [include_directories('./'), media_library_examples_inc, media_library_test_inc, test_images_inc],
  cpp_args: hailo_lib_args + ['-DTEST_IMAGES_DIR="' + test_images_install_dir + '"', platform_define],
  dependencies : verification_deps + [libdatachannel_dep, pipeline_codecs_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: unit_tests_install_dir,
)

install_subdir('analytics_tests/test_images/license_plates',
  install_dir: test_images_install_dir,
)

install_subdir('analytics_tests/test_images/license_plates_unsized',
  install_dir: test_images_install_dir,
)
