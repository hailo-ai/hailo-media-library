################################################
# Dual Sensor Single Stream Case Study
################################################

dual_sensor_single_stream_install_dir = case_studies_install_dir + '/dual_sensor_single_stream'

dual_sensor_single_stream_app_src = ['main.cpp']

executable('dual_sensor_single_stream_case_study',
  dual_sensor_single_stream_app_src,
  cpp_args : hailo_lib_args,
  dependencies : libhailo_analytics_dep,
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: dual_sensor_single_stream_install_dir,
)
