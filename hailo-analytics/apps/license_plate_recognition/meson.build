################################################
# License Plate Recognition Application
################################################

lpr_install_dir = apps_install_dir + '/license_plate_recognition'

# LPR Pipeline Builder library (reusable dependency)
lpr_pipeline_builder_lib = static_library(
  'lpr_pipeline_builder',
  sources: ['lpr_pipeline_builder.cpp'],
  cpp_args: hailo_lib_args,
  dependencies: libhailo_analytics_dep,
  gnu_symbol_visibility: 'default',
)

lpr_pipeline_builder_dep = declare_dependency(
  link_with: lpr_pipeline_builder_lib,
  include_directories: include_directories('.'),
  dependencies: libhailo_analytics_dep,
)

# LPR App
lpr_app_src = ['main.cpp']

executable('lpr_app',
  lpr_app_src,
  cpp_args : hailo_lib_args,
  dependencies : [libhailo_analytics_dep, lpr_pipeline_builder_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: lpr_install_dir,
)
