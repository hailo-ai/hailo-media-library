# Tokenizers C++ Library

This directory contains a C++ wrapper for the tokenizers library, specifically designed for CLIP text encoding.

## Files Structure

```
utils/tokenizers/
├── include/
│   ├── tokenizers_cpp.h      # C++ interface header
│   └── tokenizers_c.h        # C rust interface header
├── dist/
│   ├── tokenizer.json        # Main tokenizer configuration
│   ├── vocab.json           # Vocabulary file
│   ├── merges.txt           # BPE merges
│   ├── special_tokens_map.json
│   └── tokenizer_config.json
├── example/
│   └── tokenizer_example.cpp # Example usage
├── libtokenizers_cpp.a      # Pre-built C++ library, depend on libtokenizers_c.a
├── libtokenizers_c.a        # Pre-built C rush library
├── meson.build              # Build configuration
└── README.md                # This file
```

## Usage in Your Code

### Include the Header

```cpp
#include "tokenizers_cpp.h"
```

### Load Tokenizer from JSON

```cpp
#include <fstream>
#include <sstream>

// Read the tokenizer JSON file
std::ifstream file("path/to/tokenizer.json");
std::stringstream buffer;
buffer << file.rdbuf();
std::string json_content = buffer.str();

// Create tokenizer
auto tokenizer = tokenizers::Tokenizer::FromBlobJSON(json_content);
```

### Encode Text

```cpp
// Single text encoding
std::string text = "a photo of a cat";
auto token_ids = tokenizer->Encode(text);

// Batch encoding
std::vector<std::string> texts = {"text1", "text2", "text3"};
auto batch_token_ids = tokenizer->EncodeBatch(texts);
```

### Decode Tokens

```cpp
// Decode token IDs back to text
std::string decoded = tokenizer->Decode(token_ids);
```

### Other Operations

```cpp
// Get vocabulary size
size_t vocab_size = tokenizer->GetVocabSize();

// Convert between tokens and IDs
int32_t token_id = tokenizer->TokenToId("cat");
std::string token = tokenizer->IdToToken(token_id);
```

## Running the Example

After building, you can run the example:

```bash
# Run with default tokenizer.json path
./builddir/tokenizer_example

# Run with custom tokenizer path
./builddir/tokenizer_example /path/to/your/tokenizer.json
```

## Integration in Meson Projects

To use this library in your own meson project please check this folder's meson.build for details

```meson
# In your meson.build file
# Tokenizers dependency for other projects to use
tokenizers_inc = include_directories('include')
tokenizers_dep = declare_dependency(
  include_directories: tokenizers_inc,
  dependencies: [
    libtokenizers_cpp,    # C++ wrapper library
    libtokenizers_c       # Rust core library (despite the name)
  ],
  version: '1.0.0'
)


# Or if building as subproject
tokenizers_proj = subproject('tokenizers')
tokenizers_dep = tokenizers_proj.get_variable('tokenizers_dep')

executable('your_app',
  'your_source.cpp',
  dependencies: [tokenizers_dep]
)
```

## Notes

-   The library uses pre-built static libraries (`libtokenizers_cpp.a`)
-   The tokenizer.json file is generated for CLIP models and should work with various CLIP variants
-   The C++ interface provides a more convenient API compared to the C interface
-   Memory management is handled automatically with smart pointers
