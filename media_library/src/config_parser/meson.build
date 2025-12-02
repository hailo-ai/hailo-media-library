medialib_include_dirs += [include_directories('.')]

config_parser_sources = files(
    'config_parser.cpp',
    'json_flattener.cpp',
    'config_validator.cpp',
    'media_lib_types.cpp',
)

libconfig_parser = static_library('config_parser_internal',
    config_parser_sources,
    cpp_args: common_args,
    include_directories: medialib_include_dirs,
    dependencies: medialib_dependencies,
    pic: true,
)

medialib_components += [libconfig_parser]
