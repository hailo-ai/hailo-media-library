unit_tests_install_dir = tests_install_dir + '/unit_tests'

cpp_unit_tests_sources = [
  'test_main.cpp',
  'core_tests/buffer_tests.cpp',
  'core_tests/queue_tests.cpp',
  'core_tests/stage_tests.cpp',
  'core_tests/pipeline_tests.cpp',
  'sink_tests/udp_stage_tests.cpp',
  'codec_tests/encoder_stage_tests.cpp'
]

# Only include encoder tests on 15h
if platform == '15h'
  cpp_unit_tests_sources += 'source_tests/frontend_stage_tests.cpp'
endif

executable('hailo_analytics_unit_tests',
  cpp_unit_tests_sources,
  include_directories: incdir + [include_directories('./')],
  cpp_args: hailo_lib_args,
  dependencies : verification_deps,
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: unit_tests_install_dir,
)