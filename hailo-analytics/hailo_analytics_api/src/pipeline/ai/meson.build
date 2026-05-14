# Sources
ai_stages_sources = [
    'ai_stage.cpp',
    'postprocess_stage.cpp',
    'analytics_db_stage.cpp',
    'lightweight_tracker_stage.cpp',
    'detection_tracker_stage.cpp',
    'tracker_stage.cpp',
    'persist_stage.cpp'
]

libhailopp_dep = [meson.get_compiler('cpp').find_library('hailopp', required: true)]

# Compile lib
ai_stages_lib = static_library('hailo_analytics_ai_stages',
    ai_stages_sources,
    cpp_args: hailo_lib_args,
    include_directories: incdir,
    dependencies: [base_deps, hailo_analytics_logger_dep, libhailopp_dep],
    pic: true,
)

# Add to final lib components
hailo_analytics_components += [ai_stages_lib]

# Create dep of this lib
ai_stages_dep = declare_dependency(
  include_directories: [incdir],
  dependencies : [base_deps, hailo_analytics_logger_dep, libhailopp_dep],
  link_with : ai_stages_lib)