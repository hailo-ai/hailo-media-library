################################################
# Dynamic Privacy Mask V2 Case Study
################################################

dynamic_privacy_mask_install_dir = case_studies_install_dir + '/dynamic_privacy_mask'

dynamic_privacy_mask_app_src = ['main.cpp']

executable('dynamic_privacy_mask_case_study',
  dynamic_privacy_mask_app_src,
  cpp_args : hailo_lib_args,
  dependencies : libhailo_analytics_dep,
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: dynamic_privacy_mask_install_dir,
)
