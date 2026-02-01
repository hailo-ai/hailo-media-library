medialib_include_dirs += [include_directories('.')]
eis_incdir = [include_directories('.')]

eis_sources = files(
  'eis.cpp',
  'iir_filter.cpp',
  'gyro_device.cpp',
  'arguments_parser.cpp',
)

libeis = static_library('eis_internal',
    eis_sources,
    cpp_args: common_args,
    include_directories: medialib_include_dirs,
    dependencies: medialib_dependencies,
    pic: true,
)

medialib_components += [libeis]