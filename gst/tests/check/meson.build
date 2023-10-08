pipelines_tests = [
  [ 'pipelines/v4l2src_to_visionpreproc', false ]]

# This defines variables for the compilation
test_defines = [
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
  '-DGST_CHECK_TEST_ENVIRONMENT_BEACON="GST_PLUGIN_LOADING_WHITELIST"',
  '-DGST_TEST_FILES_PATH="' + meson.current_source_dir() + '/../files"',
  '-DGST_USE_UNSTABLE_API',
]

test_deps = gstreamer_deps + [gstcheck_dep]

# The loop will based on parameters pased on element_test to generate an executable file for each test
foreach t : pipelines_tests
  fname = '@0@.cpp'.format(t.get(0))
  test_name = t.get(0).underscorify()
  skip_test = t.get(1, false)
  extra_deps = t.get(2, [ ])
  extra_sources = t.get(3, [ ])
  timeout_for_test = t.get(4, '20')
  
  if not skip_test
    env = environment()
    # Defines timeout for test
    env.set('CK_DEFAULT_TIMEOUT', timeout_for_test)
    # Defines from where to find the packages to get the plugins
    env.set('GST_PLUGIN_PATH_1_0', [meson.global_build_root()])
    # Defines the system to use to store data using the gsetting to control an app configurations
    # By setting the value to memory, not data from running the test will be saved and afect the behavior
    # Of other applications 
    env.set('GSETTINGS_BACKEND', 'memory')
    executable(test_name, fname, extra_sources,
      include_directories : [],
      c_args : test_defines,
      dependencies : test_deps + extra_deps,
    )
  endif
endforeach
