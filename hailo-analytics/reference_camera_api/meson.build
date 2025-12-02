# api includes
include_path = 'include'
incdir = [include_directories(include_path)]
install_subdir(include_path, install_dir : get_option('includedir') + '/hailo')

subdir('src/perfetto')
subdir('src/logger')
subdir('src/pipeline_infra')