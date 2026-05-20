################################################
# APP WRAPPER SOURCE
################################################

# Protobuf and gRPC setup for client
protoc = find_program('protoc')
grpc_cpp_plugin = find_program('grpc_cpp_plugin')
protobuf_dep = dependency('protobuf')
grpc_dep = dependency('grpc++')

proto_install_dir = get_option('includedir') + '/hailo/media_library'

generated_protobuf_sources = custom_target('media_library_proto',
  input: 'src/service/protos/media_library.proto',
  output: ['media_library.pb.cc', 'media_library.pb.h',
           'media_library.grpc.pb.cc', 'media_library.grpc.pb.h'],
  command: [protoc,
            '--proto_path=@CURRENT_SOURCE_DIR@/src/service/protos',
            '--cpp_out=@OUTDIR@',
            '--grpc_out=@OUTDIR@',
            '--plugin=protoc-gen-grpc=' + grpc_cpp_plugin.full_path(),
            '@INPUT@'],
  install: true,
  install_dir: [false, proto_install_dir, false, proto_install_dir],
)

install_headers('src/service/proto_converters.hpp', subdir: 'hailo/media_library')

sources = [
  'src/encoder/encoder_api.cpp',
  'src/encoder/encoder_api_impl.cpp',
  'src/frontend/frontend.cpp',
  'src/media_library/media_library.cpp',
  'src/media_library/media_library_api_types.cpp',
  'src/utils/utils.cpp',
  'src/service/media_library_client.cpp',
  'src/service/media_library_service_buffer_uds.cpp',
  'src/service/client_buffer_manager.cpp',
  'src/service/proto_converters.cpp'
  ]

api_include_dirs = [include_directories('./include/')]
medialib_include_dirs += api_include_dirs

# Add proto generated includes to medialib_include_dirs
medialib_include_dirs += [include_directories('.')]

libapi_wrapper = static_library('api_wrapper_impl',
    [sources, generated_protobuf_sources],
    cpp_args: common_args,
    include_directories: medialib_include_dirs,
    dependencies: [medialib_dependencies, protobuf_dep, grpc_dep],
    pic: true,
)

medialib_components += [libapi_wrapper]
medialib_dependencies += [protobuf_dep, grpc_dep]

install_subdir('include/media_library', install_dir: get_option('includedir') + '/hailo')
config_example_files = run_command('find', 'examples/config_examples', '-maxdepth', '1', '-type', 'f', '-name', '*.json').stdout().split()
install_data(config_example_files, install_dir: get_option('bindir'))
