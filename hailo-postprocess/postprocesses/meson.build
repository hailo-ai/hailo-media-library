post_proc_install_dir = get_option('post_processes_install_dir')

################################################
# DETECTION SOURCES - YOLOV5M
################################################
yolo_hailortpp_sources = [
    'detection/yolo_hailortpp.cpp',
    'common/nms.cpp',
    'common/file_reader.cpp',
]

shared_library('yolo_hailortpp_post',
    yolo_hailortpp_sources,
    cpp_args : hailo_lib_args,
    include_directories: [include_directories('./')],
    dependencies : [libhailo_postprocess_tools_dep],
    gnu_symbol_visibility : 'default',
    install: true,
    install_dir: post_proc_install_dir,
)

################################################
# FACIAL LANDMARKING SOURCES
################################################

mediapipe_post_sources = [
    'facial_landmarking/mediapipe_landmarks_post.cpp',
    'common/tensors.cpp',
]

shared_library('mediapipe_post',
    mediapipe_post_sources,
    cpp_args : hailo_lib_args,
    include_directories: [include_directories('./')],
    dependencies : [libhailo_postprocess_tools_dep],
    gnu_symbol_visibility : 'default',
    install: true,
    install_dir: post_proc_install_dir,
)

################################################
# SCRFD FACE DETECTION + LANDMARKS SOURCES
################################################
scrfd_post_sources = [
    'facial_landmarking/scrfd_post.cpp',
    'common/tensors.cpp',
    'common/nms.cpp',
    'common/file_reader.cpp',
]

shared_library('scrfd_post',
    scrfd_post_sources,
    cpp_args : hailo_lib_args,
    include_directories: [include_directories('./')],
    dependencies : [libhailo_postprocess_tools_dep],
    gnu_symbol_visibility : 'default',
    install: true,
    install_dir: post_proc_install_dir,
)

################################################
# DETECTION YOLO SOURCES
################################################
detection_new_api_post_sources = [
  'detection/yolo_postprocess.cpp',
  'detection/yolo_output.cpp',
  'common/nms.cpp',
  'common/file_reader.cpp',
]

shared_library('yolo_post',
    detection_new_api_post_sources,
    cpp_args : hailo_lib_args + ['-Wno-psabi'],
    include_directories: [include_directories('./')],
    dependencies : [libhailo_postprocess_tools_dep],
    gnu_symbol_visibility : 'default',
    install: true,
    install_dir: post_proc_install_dir,
)

################################################
# CLIP SOURCES
################################################
zmq_dep = dependency('libzmq', method : 'pkg-config')
json_dep = meson.get_compiler('cpp').find_library('libnlohmann_json_schema_validator', required: true, dirs: '/usr/lib/')

clip_sources = [
    'clip/clip.cpp',
]

shared_library('clip_post',
    clip_sources,
    cpp_args : hailo_lib_args,
    include_directories: [include_directories('./')],
    dependencies :[zmq_dep, json_dep, libhailo_postprocess_tools_dep],
    gnu_symbol_visibility : 'default',
    install: true,
    install_dir: post_proc_install_dir,
)

################################################
# CLIP Gen SOURCES
################################################
clip_gen_sources = [
    'clip/clip_gen.cpp',
]

shared_library('clipgen_post',
    clip_gen_sources,
    cpp_args : hailo_lib_args,
    include_directories: [include_directories('./')],
    dependencies :[libhailo_postprocess_tools_dep],
    gnu_symbol_visibility : 'default',
    install: true,
    install_dir: post_proc_install_dir,
)

################################################
# LINKNET SOURCES
################################################
linknet_post_sources = [
    'semantic_segmentation/linknet.cpp',
    'common/tensors.cpp',
]

shared_library('linknet_post',
    linknet_post_sources,
    cpp_args : hailo_lib_args,
    include_directories: [include_directories('./')],
    dependencies : [libhailo_postprocess_tools_dep],
    gnu_symbol_visibility : 'default',
    install: true,
    install_dir: post_proc_install_dir,
)

################################################
# FACE RECOGNITION ARCFACE SOURCES
################################################
arcface_post_sources = [
    'face_recognition/arcface_post.cpp',
    'common/tensors.cpp',
    'common/math.cpp',
    'common/file_reader.cpp',
]

shared_library('arcface_post',
    arcface_post_sources,
    cpp_args : hailo_lib_args,
    include_directories: [include_directories('./')],
    dependencies : [json_dep, libhailo_postprocess_tools_dep],
    gnu_symbol_visibility : 'default',
    install: true,
    install_dir: post_proc_install_dir,
)

################################################
# LICENSE PLATE RECOGNITION OCR SOURCES
################################################
ocr_post_sources = [
    'license_plate_recognition/ocr_post.cpp',
    'common/tensors.cpp',
]

shared_library('ocr_post',
    ocr_post_sources,
    cpp_args : hailo_lib_args,
    include_directories: [include_directories('./')],
    dependencies : [libhailo_postprocess_tools_dep],
    gnu_symbol_visibility : 'default',
    install: true,
    install_dir: post_proc_install_dir,
)
