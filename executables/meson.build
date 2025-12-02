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
  '../media_library/src/eis_library/gyro_device.cpp',
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

# Encoder Example
encoder_example_src = files('../api/examples/encoder_example.cpp')
executable('encoder_example',
  encoder_example_src,
  cpp_args: common_args,
  dependencies: [libmedialib_dep],
  install: true,
  install_dir: get_option('bindir'),
)

# Polygon Example
polygon_example_src = files('../api/examples/polygon_example.cpp')
executable('polygon_example',
  polygon_example_src,
  cpp_args: common_args,
  dependencies: [libmedialib_dep],
  install: true,
  install_dir: get_option('bindir'),
)

# Calculate OSD Text Size
calculate_size_osd_src = files('../api/examples/calculate_text_size_osd.cpp')
executable('calc_osd_text_size',
  calculate_size_osd_src,
  cpp_args: common_args,
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
