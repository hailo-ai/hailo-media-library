################################################
# Face Landmarks Application
################################################

face_landmarks_install_dir = apps_install_dir + '/face_landmarks'

# Face Landmarks Pipeline Builder library (reusable dependency)
face_landmarks_pipeline_builder_lib = static_library(
  'face_landmarks_pipeline_builder',
  sources: ['face_landmarks_pipeline_builder.cpp'],
  cpp_args: hailo_lib_args,
  dependencies: libhailo_analytics_dep,
  gnu_symbol_visibility: 'default',
)

face_landmarks_pipeline_builder_dep = declare_dependency(
  link_with: face_landmarks_pipeline_builder_lib,
  include_directories: include_directories('.'),
  dependencies: libhailo_analytics_dep,
)

# Face Landmarks App
face_landmarks_app_src = ['main.cpp']

executable('face_landmarks_app',
  face_landmarks_app_src,
  cpp_args : hailo_lib_args,
  dependencies : [libhailo_analytics_dep, face_landmarks_pipeline_builder_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: face_landmarks_install_dir,
)

# Face Landmarks Dual Sensor App
executable('face_landmarks_dual_sensor_app',
  'face_landmarks_dual_sensor_app/main.cpp',
  cpp_args : hailo_lib_args,
  dependencies : [libhailo_analytics_dep, face_landmarks_pipeline_builder_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: face_landmarks_install_dir + '/face_landmarks_dual_sensor_app',
)
