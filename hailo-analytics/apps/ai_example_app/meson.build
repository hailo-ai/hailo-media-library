################################################
# HAILO 15 AI EXAMPLE APP
################################################

ai_example_install_dir = apps_install_dir + '/ai_example_app'

# AI Pipeline Builder library (reusable dependency)
ai_pipeline_builder_lib = static_library(
  'ai_pipeline_builder',
  sources: ['ai_pipeline_builder.cpp'],
  cpp_args: hailo_lib_args,
  dependencies: libhailo_analytics_dep,
  gnu_symbol_visibility: 'default',
)

ai_pipeline_builder_dep = declare_dependency(
  link_with: ai_pipeline_builder_lib,
  include_directories: include_directories('.'),
  dependencies: libhailo_analytics_dep,
)

# AI Example App
ai_example_app_src = ['main.cpp']

executable('ai_example_app',
  ai_example_app_src,
  cpp_args : hailo_lib_args,
  dependencies : [libhailo_analytics_dep, ai_pipeline_builder_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: ai_example_install_dir,
)

# Dual Sensor AI Example App
ai_example_dual_sensor_app_src = ['ai_example_dual_sensor_app/main.cpp']

executable('ai_example_dual_sensor_app',
  ai_example_dual_sensor_app_src,
  cpp_args : hailo_lib_args,
  dependencies : [libhailo_analytics_dep, ai_pipeline_builder_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: ai_example_install_dir + '/ai_example_dual_sensor_app',
)
