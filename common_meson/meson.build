debug_args = ['-g', '-O0', '-U_FORTIFY_SOURCE']
common_args = ['-DHAVE_CONFIG_H']

core_conf = configuration_data()
core_conf.set('PACKAGE', '"@0@"'.format(meson.project_name()))
core_conf.set('VERSION', '"@0@"'.format(meson.project_version()))

build_mode = 'release'
if get_option('buildtype') == 'debug'
    common_args += debug_args
    build_mode = 'debug'
endif

git_commit_and_dirty_status = run_command('git', 'describe', '--dirty', '--always', check: true).stdout().strip()
git_branch = run_command('git', 'rev-parse', '--abbrev-ref', 'HEAD', check: true).stdout().strip()
git_metadata = 'Branch: ' + git_branch + ' Commit + Dirty: ' + git_commit_and_dirty_status + ' Build mode: ' + build_mode 

git_metadata_lib = static_library(
    'git_metadata',
    'git_metadata.cpp',
    install: false,
    cpp_args: ['-DGIT_METADATA="' + git_metadata + '"']
)

dsp_dep = meson.get_compiler('c').find_library('libhailodsp', required: false, dirs: '/usr/lib/')

expected_dep = meson.get_compiler('cpp').find_library('tl', required: false, dirs: '/usr/include/')
perfetto_dep = meson.get_compiler('cpp').find_library('perfetto', required: false, dirs: '/usr/lib/')

if perfetto_dep.found()
    common_args += ['-DHAVE_PERFETTO']
else
    common_args += ['-DPERFETTO_NOT_FOUND']
endif

libhailort_dep = [meson.get_compiler('cpp').find_library('hailort', required: false)]
gsthailort_dep = [meson.get_compiler('cpp').find_library('libgsthailo', required: false)]
bsp_thorttling_manager_dep = meson.get_compiler('cpp').find_library('libhailo-throttling', required: true,  dirs: '/usr/lib/')

hailort_dep = [
    libhailort_dep,
    gsthailort_dep
]
