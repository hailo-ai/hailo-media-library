# Sources
objects_src = [
    'json_config.cpp'
]

# Compile lib
objects_lib = static_library('hailo_objects',
    objects_src,
    cpp_args: hailo_lib_args,
    include_directories: incdir,
    dependencies: [base_deps, hailo_postprocess_logger_dep],
    pic: true,
)

# Add to final lib components
hailo_postprocess_tools_components += [objects_lib]
