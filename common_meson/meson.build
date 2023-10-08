debug_args = ['-g', '-O0']
common_args = ['-DHAVE_CONFIG_H']

core_conf = configuration_data()
core_conf.set('PACKAGE', '"@0@"'.format(meson.project_name()))
core_conf.set('VERSION', '"@0@"'.format(meson.project_version()))

dsp_dep = meson.get_compiler('c').find_library('libhailodsp', required: false, dirs: '/usr/lib/')

expected_dep = meson.get_compiler('cpp').find_library('tl', required: false, dirs: '/usr/include/')