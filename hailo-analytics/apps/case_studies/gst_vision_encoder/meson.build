################################################
# GStreamer Vision + Encoder Case Study
################################################

gst_vision_encoder_install_dir = case_studies_install_dir + '/gst_vision_encoder'

gst_vision_encoder_app_src = ['main.cpp']

executable('gst_vision_encoder_case_study',
  gst_vision_encoder_app_src,
  cpp_args : hailo_lib_args,
  dependencies : libhailo_analytics_dep,
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: gst_vision_encoder_install_dir,
)

install_data(
  'gst_vision_encoder_pipeline.sh',
  install_dir: gst_vision_encoder_install_dir,
  install_mode: 'rwxr-xr-x',
)
