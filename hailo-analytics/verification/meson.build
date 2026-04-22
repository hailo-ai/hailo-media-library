# GoogleTest (GTest/GMock) Dependency
gtest_dep = dependency('gtest', method : 'pkg-config', required: true)
gtest_main_dep = dependency('gtest_main', method : 'pkg-config', required: true)
gmock_dep = dependency('gmock', method : 'pkg-config', required: false)

verification_deps = [gtest_dep, gtest_main_dep, gmock_dep, libhailo_analytics_dep, opencv_dep]

tests_install_dir = '/home/root/tests'

subdir('unit_tests')