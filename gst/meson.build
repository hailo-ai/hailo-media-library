if not dsp_dep.found()
    error('libhailodsp is required but not found')
endif

# Generate config.h file for GStreamer
configure_file(output : 'config.h', configuration : core_conf)

################################################
# Gstreamer Media Library
################################################

utils_sources = [
    'buffer_utils/buffer_utils.cpp',
    'buffer_utils/gsthailobuffermeta.cpp',
    'osd/osd.cpp',
    'osd/alignment.cpp',
    'utils/gsthailodmabufallocator.cpp',
    'utils/gsthailodmabufferpool.cpp',
    'common/gstmedialibptrs.cpp',
    'common/gstmedialibcommon.cpp',
    'osd/impl/blender_impl.cpp',
    'osd/impl/custom_overlay_impl.cpp',
    'osd/impl/datetime_overlay_impl.cpp',
    'osd/impl/image_overlay_impl.cpp',
    'osd/impl/overlay_impl.cpp',
    'osd/impl/text_overlay_impl.cpp',
    'osd/impl/simple_text_overlay_impl.cpp',
    'osd/impl/background_text_overlay_impl.cpp',
    
    # DSP Related sources
    'dsp/gsthailodspbufferpool.cpp',
    'dsp/gsthailodspbufferpoolutils.cpp',
    'dsp/gsthailodspbasetransform.cpp',
]

# GStreamer utils - Static library
gst_utils_include_dirs = [
    include_directories('./buffer_utils'), 
    include_directories('./osd'), 
    include_directories('./common'),
    include_directories('./dsp'),
]
medialib_include_dirs += gst_utils_include_dirs

libgst_utils = static_library('gst_utils_internal',
    utils_sources,
    cpp_args: common_args,
    link_args: ['-lm'],
    dependencies: medialib_dependencies,
    include_directories: medialib_include_dirs,
    pic: true,
)

medialib_components += [libgst_utils]

headers_to_install = [
    'common/gstmedialibcommon.hpp',
    'common/gstmedialibptrs.hpp',
    'buffer_utils/buffer_utils.hpp',
    'buffer_utils/gsthailobuffermeta.hpp',
    'osd/osd.hpp',
    'dsp/gsthailodspbasetransform.hpp',
    'dsp/gsthailodspbufferpool.hpp',
    'dsp/gsthailodspbufferpoolutils.hpp'
]
install_headers(headers_to_install, subdir: 'hailo')
