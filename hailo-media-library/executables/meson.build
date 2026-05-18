################################################
# EXECUTABLES - Built after libmedialib.so
################################################

################################################
# Tools
################################################

# Snapshot CLI
snapshot_cli_sources = files('../media_library/src/snapshot/snapshot_cli.cpp')
executable('snapshot_cli',
  snapshot_cli_sources,
  cpp_args: common_args,
  dependencies: [libmedialib_dep],
  install: true,
  install_dir: get_option('bindir'),
)

# Gyro Calibration
gyro_sources = files(
  '../media_library/src/eis_library/gyro_calibration.cpp',
  '../media_library/src/eis_library/arguments_parser.cpp'
)
executable('gyro_calibration',
  gyro_sources,
  cpp_args: common_args,
  dependencies: [libmedialib_dep, libiio_dep],
  install: true,
  install_dir: get_option('bindir'),
)

################################################
# API Examples
################################################

# Common examples library
examples_common_inc = include_directories('../api/examples')
examples_common_sources = files('../api/examples/common/common.cpp')
examples_common_lib = static_library('examples_common',
  examples_common_sources,
  cpp_args: common_args,
  include_directories: examples_common_inc,
  dependencies: [libmedialib_dep],
)

# Frontend Example
frontend_example_src = files('../api/examples/frontend_example.cpp')
executable('frontend_example',
  frontend_example_src,
  cpp_args: common_args,
  dependencies: [libmedialib_dep],
  install: true,
  install_dir: get_option('bindir'),
)

# JPEG Frontend Example
executable('jpeg_frontend_example',
  frontend_example_src,
  cpp_args: common_args + ['-DUSE_JPEG_JSONS'],
  dependencies: [libmedialib_dep],
  install: true,
  install_dir: get_option('bindir'),
)

# Switch Encoder Frontend Example
executable('switch_encoder_frontend_example',
  frontend_example_src,
  cpp_args: common_args + ['-DUSE_SWITCH_ENCODER_EXAMPLE'],
  dependencies: [libmedialib_dep],
  install: true,
  install_dir: get_option('bindir'),
)

# Config Tuning GStreamer Application
config_tuning_gst_application_src = files('../api/examples/config_tuning_gst_application.cpp')
executable('config_tuning_gst_application',
  config_tuning_gst_application_src,
  cpp_args: common_args,
  dependencies: [libmedialib_dep],
  install: true,
  install_dir: get_option('bindir'),
)

# Profile Switching Example
profile_switching_src = files('../api/examples/profile_switching/profile_switching.cpp')
executable('profile_switching',
  profile_switching_src,
  cpp_args: common_args,
  include_directories: examples_common_inc,
  dependencies: [libmedialib_dep],
  link_with: [examples_common_lib],
  install: true,
  install_dir: get_option('bindir'),
)

# OSD Example
osd_example_src = files('../api/examples/osd_example/osd_example.cpp')
executable('osd_example',
  osd_example_src,
  cpp_args: common_args,
  include_directories: examples_common_inc,
  dependencies: [libmedialib_dep],
  link_with: [examples_common_lib],
  install: true,
  install_dir: get_option('bindir'),
)

# Basic Recording Example
frontend_example_src = files('../api/examples/basic_recording_to_file/basic_recording_to_file.cpp')
executable('basic_h264_recording_to_file',
  frontend_example_src,
  cpp_args: common_args,
  include_directories: examples_common_inc,
  dependencies: [libmedialib_dep],
  install: true,
  install_dir: get_option('bindir'),
)

# JPEG Basic Recording Example
frontend_example_src = files('../api/examples/basic_recording_to_file/basic_recording_to_file.cpp')
executable('basic_jpeg_recording_to_file',
  frontend_example_src,
  cpp_args: common_args + ['-DUSE_JPEG_JSONS'],
  include_directories: examples_common_inc,
  dependencies: [libmedialib_dep],
  install: true,
  install_dir: get_option('bindir'),
)

# Dynamic Control Example
frontend_example_src = files('../api/examples/dynamic_controls_example/dynamic_controls_example.cpp')
executable('dynamic_controls_example',
  frontend_example_src,
  cpp_args: common_args,
  include_directories: examples_common_inc,
  dependencies: [libmedialib_dep],
  install: true,
  install_dir: get_option('bindir'),
)

# OSD Pricavy Example
frontend_example_src = files('../api/examples/osd_privacy_example/osd_privacy_example.cpp')
executable('osd_privacy_example',
  frontend_example_src,
  cpp_args: common_args,
  include_directories: examples_common_inc,
  dependencies: [libmedialib_dep],
  install: true,
  install_dir: get_option('bindir'),
)

# Profile Switching Example
frontend_example_src = files('../api/examples/profile_switching_example/profile_switching_example.cpp')
executable('profile_switching_example',
  frontend_example_src,
  cpp_args: common_args,
  include_directories: examples_common_inc,
  dependencies: [libmedialib_dep],
  install: true,
  install_dir: get_option('bindir'),
)

# JPEG Profile Switching Example
frontend_example_src = files('../api/examples/profile_switching_example/profile_switching_example.cpp')
executable('jpeg_profile_switching_example',
  frontend_example_src,
  cpp_args: common_args + ['-DUSE_JPEG_JSONS'],
  include_directories: examples_common_inc,
  dependencies: [libmedialib_dep],
  install: true,
  install_dir: get_option('bindir'),
)

# Encoder Profile Switching Example
frontend_example_src = files('../api/examples/profile_switching_example/profile_switching_example.cpp')
executable('encoder_profile_switching_example',
  frontend_example_src,
  cpp_args: common_args + ['-DUSE_SWITCH_ENCODER_EXAMPLE'],
  include_directories: examples_common_inc,
  dependencies: [libmedialib_dep],
  install: true,
  install_dir: get_option('bindir'),
)

# Rotation Example
frontend_example_src = files('../api/examples/rotation_example/rotation_example.cpp')
executable('rotation_example',
  frontend_example_src,
  cpp_args: common_args,
  include_directories: examples_common_inc,
  dependencies: [libmedialib_dep],
  link_with: [examples_common_lib],
  install: true,
  install_dir: get_option('bindir'),
)

# JPEG Rotation Example
frontend_example_src = files('../api/examples/rotation_example/rotation_example.cpp')
executable('jpeg_rotation_example',
  frontend_example_src,
  cpp_args: common_args + ['-DUSE_JPEG_JSONS'],
  include_directories: examples_common_inc,
  dependencies: [libmedialib_dep],
  install: true,
  install_dir: get_option('bindir'),
)

# Resolution Change Example
frontend_example_src = files('../api/examples/resolution_change_example/resolution_change_example.cpp')
executable('resolution_change_example',
  frontend_example_src,
  cpp_args: common_args,
  include_directories: examples_common_inc,
  dependencies: [libmedialib_dep],
  install: true,
  install_dir: get_option('bindir'),
)

# JPEG Resolution Change Example
frontend_example_src = files('../api/examples/resolution_change_example/resolution_change_example.cpp')
executable('jpeg_resolution_change_example',
  frontend_example_src,
  cpp_args: common_args + ['-DUSE_JPEG_JSONS'],
  include_directories: examples_common_inc,
  dependencies: [libmedialib_dep],
  install: true,
  install_dir: get_option('bindir'),
)

# GStreamer Vision + Encoder Example
gst_vision_encoder_example_src = files('../api/examples/gst_vision_encoder/gst_vision_encoder_example.cpp')
executable('gst_vision_encoder_example',
  gst_vision_encoder_example_src,
  cpp_args: common_args,
  dependencies: [libmedialib_dep],
  install: true,
  install_dir: get_option('bindir'),
)

