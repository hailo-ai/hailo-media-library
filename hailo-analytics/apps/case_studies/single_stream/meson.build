################################################
# Detection Case Study
################################################

single_stream_install_dir = case_studies_install_dir + '/single_stream'

single_stream_app_src = ['main.cpp']

executable('single_stream_case_study',
  single_stream_app_src,
  cpp_args : hailo_lib_args,
  dependencies : libhailo_analytics_dep,
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: single_stream_install_dir,
)
