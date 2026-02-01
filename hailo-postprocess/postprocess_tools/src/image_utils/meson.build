# Sources
image_utils_src = [
    'image.cpp'
]

# Compile lib
image_utils_lib = static_library('hailo_image_utils',
    image_utils_src,
    cpp_args: hailo_lib_args,
    include_directories: incdir,
    dependencies: [base_deps, opencv_dep],
    pic: true,
)

# Add to final lib components
hailo_postprocess_tools_components += [image_utils_lib]

# Create dep of this lib
image_utils_dep = declare_dependency(
  include_directories: [incdir],
  dependencies : [base_deps, opencv_dep],
  link_with : image_utils_lib)