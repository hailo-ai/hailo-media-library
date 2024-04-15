debug_args = ['-g', '-O0']
common_args = ['-DHAVE_CONFIG_H']

core_conf = configuration_data()
core_conf.set('PACKAGE', '"@0@"'.format(meson.project_name()))
core_conf.set('VERSION', '"@0@"'.format(meson.project_version()))

dsp_dep = meson.get_compiler('c').find_library('libhailodsp', required: false, dirs: '/usr/lib/')

expected_dep = meson.get_compiler('cpp').find_library('tl', required: false, dirs: '/usr/include/')

# Hailort Arguments
sysroot_arg = []
if meson.is_cross_build()
    cmd_array = meson.get_compiler('cpp').cmd_array()
    foreach arg : cmd_array
        if arg.startswith('--sysroot=')
            sysroot = arg.split('=')[1]
            break
        endif
    endforeach
    message('Cross building using sysroot: ' + sysroot)
    if sysroot != ''
        sysroot_arg += '--sysroot=' + sysroot
    endif
endif

libhailort_dep = [meson.get_compiler('cpp').find_library('hailort', required: false)]
gsthailort_dep = [meson.get_compiler('cpp').find_library('libgsthailo', required: false, dirs: sysroot + '/usr/lib/gstreamer-1.0/')]

hailort_dep = [
    libhailort_dep,
    gsthailort_dep
]