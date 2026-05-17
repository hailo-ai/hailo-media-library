# Protobuf setup for analytics metadata schema.
# Uses meson's generator() primitive so any .proto added to the protos/ dir
# can be processed with a single call: proto_gen.process('protos/<name>.proto').
protoc = find_program('protoc')
protobuf_dep = dependency('protobuf')

proto_gen = generator(protoc,
    output: ['@BASENAME@.pb.cc', '@BASENAME@.pb.h'],
    arguments: ['--proto_path=@CURRENT_SOURCE_DIR@/protos',
                '--cpp_out=@BUILD_DIR@',
                '@INPUT@'])

generated_analytics_metadata_proto = proto_gen.process('protos/analytics_metadata.proto')

# Sources
codecs_sources = [
    'encoder_stage.cpp',
    'analytic_metadata_packager_stage.cpp'
]

# Compile lib
codecs_lib = static_library('hailo_analytics_pipeline_codecs',
    [codecs_sources, generated_analytics_metadata_proto],
    cpp_args: hailo_lib_args,
    include_directories: [incdir, include_directories('.')],
    dependencies: [base_deps, hailo_analytics_logger_dep, protobuf_dep],
    pic: true,
)

# Add to final lib components
hailo_analytics_components += [codecs_lib]

# Create dep of this lib — propagate protobuf so receivers that parse
# the generated message types pick up the runtime automatically.
pipeline_codecs_dep = declare_dependency(
  include_directories: [incdir, include_directories('.')],
  dependencies : [base_deps, hailo_analytics_logger_dep, protobuf_dep],
  link_with : codecs_lib,
  sources: [generated_analytics_metadata_proto])
