################################################
# Custom Stage Case Study
################################################

custom_stage_install_dir = case_studies_install_dir + '/custom_stage'

custom_stage_app_src = ['main.cpp']

executable('custom_stage_case_study',
  custom_stage_app_src,
  cpp_args : hailo_lib_args,
  dependencies : libhailo_analytics_dep,
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: custom_stage_install_dir,
)
