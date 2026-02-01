################################################
# File Source to UDP Case Study
################################################

file_source_to_udp_install_dir = case_studies_install_dir + '/file_source_to_udp'

file_source_to_udp_app_src = ['main.cpp']

executable('file_source_to_udp_case_study',
  file_source_to_udp_app_src,
  cpp_args : hailo_lib_args,
  dependencies : libhailo_analytics_dep,
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: file_source_to_udp_install_dir,
)
