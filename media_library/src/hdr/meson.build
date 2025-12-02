frontend_sources += files('hdr_manager.cpp')
hdr_incdir = [include_directories('.')]

if platform == '15h'
    hdr_incdir += include_directories('impl/nncore/')
    medialib_include_dirs += [include_directories('impl/nncore/')]
    frontend_sources += files([
        'impl/nncore/hdr_manager_impl.cpp',
        'impl/nncore/hrt_stitcher/hrt_stitcher.cpp'
    ])
elif platform == '15l'
    hdr_incdir += include_directories('impl/isp/')
    medialib_include_dirs += [include_directories('impl/isp/')]
    frontend_sources += files([
        'impl/isp/hdr_manager_impl.cpp'
    ])
else
    error('Unsupported platform for HDR implementation')
endif

medialib_include_dirs += hdr_incdir