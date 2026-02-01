################################################
# Detection Case Study
################################################

detection_install_dir = case_studies_install_dir + '/detection'

detection_app_src = ['main.cpp']

executable('detection_case_study',
  detection_app_src,
  cpp_args : hailo_lib_args,
  dependencies : libhailo_analytics_dep,
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: detection_install_dir,
)
