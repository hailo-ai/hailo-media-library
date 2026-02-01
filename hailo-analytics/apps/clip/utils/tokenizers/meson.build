#####################################################
# TOKENIZERS LIBRARY AND EMBEDDING LOOKUP EXAMPLE
#####################################################

libtokenizers_cpp = compiler.find_library('tokenizers_cpp', required: true, dirs: '/usr/lib')
libtokenizers_c = compiler.find_library('tokenizers_c', required: true, dirs: '/usr/lib')

tokenizer_example_install_dir = apps_install_dir + '/tokenizer_example'

# Build the examples
tokenizer_example_exe = executable(
  'tokenizer_example',
  'example/tokenizer_example.cpp',
  dependencies: [tokenizers_dep],
  cpp_args: ['-std=c++17'],
  install: true,
  install_dir: tokenizer_example_install_dir
)

#embedding_lookup_example_exe = executable(
#  'embedding_lookup_example',
#  'example/embedding_lookup_example.cpp',
#  dependencies: [libhailort_dep],
#  cpp_args: ['-std=c++17'],
#  install: true,
#  install_dir: get_option('bindir')
#)
