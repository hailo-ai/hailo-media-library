medialib_include_dirs += [include_directories('.')]

dis_incdir = [include_directories('.')]

dis_sources = files(
  'dis.cpp',
  'dis_interface.cpp',
)

# Build as static library with PIC enabled
libdis = static_library('dis_internal',
    dis_sources,
    cpp_args: common_args,
    include_directories: medialib_include_dirs,
    dependencies: medialib_dependencies,
    pic: true,
)

# Register component with global array
medialib_components += [libdis]
