################################################
# Face Landmarks Application
################################################

face_landmarks_install_dir = apps_install_dir + '/face_landmarks'

face_landmarks_app_src = ['main.cpp']

executable('face_landmarks_app',
  face_landmarks_app_src,
  cpp_args : hailo_lib_args,
  dependencies : libhailo_analytics_dep,
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: face_landmarks_install_dir,
)
