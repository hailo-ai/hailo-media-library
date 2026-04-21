#!/usr/bin/env python3
"""
CLIP Text Encoder Full Test Script

This script performs a comprehensive comparison between CLIP PyTorch and Hailo ONNX implementations:
1. Allows user to enter custom prompt (default: "a photo of a cat")
2. Generates sentence embeddings using tokenization + embedding lookup from bin files
3. Compares outputs at each stage: sentence embedding, before projection, after projection, after L2 normalization
4. Saves intermediate results as .npy files

Supports OpenAI CLIP, OpenCLIP (LAION), and HuggingFace Transformers models.

Requirements:
- Bin files in specified folder: embedding_lookup.bin, projection_matrix_weights.bin, projection_matrix_bias.bin
- Optional sentence embedding .npy file (if not provided, generates from tokenization)

Usage Examples:

- Classic Compare Example (OpenAI CLIP):
python clip_text_encoder_full_test.py --prompt "a photo of a cat" --bin-folder ./bin_files --hailo-onnx-path ./clip_text_encoder.onnx --model ViT-B/32 --output-dir ./output

- Using OpenCLIP/LAION model:
python clip_text_encoder_full_test.py --prompt "a photo of a cat" --bin-folder ./bin_files --hailo-onnx-path ./clip_text_encoder.onnx --model laion/CLIP-ViT-L-14-laion2B-s32B-b82K --output-dir ./output

- Using HuggingFace transformers model (for Hailo ONNX exported from HuggingFace):
python clip_text_encoder_full_test.py --prompt "a photo of a cat" --bin-folder ./bin_files --hailo-onnx-path ./clip_text_encoder.onnx --model laion/CLIP-ViT-L-14-laion2B-s32B-b82K-hf --output-dir ./output

- Providing your own sentence embedding .npy file:
python clip_text_encoder_full_test.py --prompt "a photo of a cat" --bin-folder ./bin_files --hailo-onnx-path ./clip_text_encoder.onnx --sentence-embedding-path ./my_sentence_embedding.npy --model ViT-B/32 --output-dir ./output

"""

import clip
import torch
import onnxruntime as ort
import numpy as np
import argparse
import sys
import struct
import os

# Try to import transformers for HuggingFace models support
try:
    from transformers import CLIPModel, CLIPTokenizer
    TRANSFORMERS_AVAILABLE = True
except ImportError:
    TRANSFORMERS_AVAILABLE = False
    print("Warning: transformers not available. HuggingFace CLIP models will not be supported.")
    print("Install with: pip install transformers")


MODEL_CONFIGS = {
    "ViT-B/32": {
        "model_name": "ViT-B/32",
        "embedding_dim": 512,
        "description": "Vision Transformer Base with 32x32 patches",
        "library": "clip"
    },
    "RN50x4": {
        "model_name": "RN50x4", 
        "embedding_dim": 640,
        "description": "ResNet-50 with 4x width multiplier",
        "library": "clip"
    },
    "laion/CLIP-ViT-L-14-laion2B-s32B-b82K-hf": {
        "model_name": "laion/CLIP-ViT-L-14-laion2B-s32B-b82K",
        "embedding_dim": 768,
        "description": "LAION Vision Transformer Large trained on LAION-2B dataset (HuggingFace)",
        "library": "transformers",
        "hf_model_id": "laion/CLIP-ViT-L-14-laion2B-s32B-b82K"
    },
    # Example of how to add more models:
    # OpenAI CLIP models:
    # "ViT-L/14": {
    #     "model_name": "ViT-L/14",
    #     "embedding_dim": 768,
    #     "description": "Vision Transformer Large with 14x14 patches",
    #     "library": "clip"
    # },
    # "ViT-L/14@336px": {
    #     "model_name": "ViT-L/14@336px",
    #     "embedding_dim": 768,
    #     "description": "Vision Transformer Large with 14x14 patches at 336px resolution",
    #     "library": "clip"
    # },
    # "RN50": {
    #     "model_name": "RN50",
    #     "embedding_dim": 1024,
    #     "description": "ResNet-50",
    #     "library": "clip"
    # },
    # HuggingFace transformers models:
    # "openai/clip-vit-base-patch32-hf": {
    #     "model_name": "openai/clip-vit-base-patch32",
    #     "embedding_dim": 512,
    #     "description": "OpenAI CLIP ViT-B/32 from HuggingFace",
    #     "library": "transformers",
    #     "hf_model_id": "openai/clip-vit-base-patch32"
    # },
}


def get_supported_models():
    """Get list of supported model names"""
    return list(MODEL_CONFIGS.keys())


def get_model_config(model_name):
    """Get model configuration for the specified model"""
    if model_name not in MODEL_CONFIGS:
        supported = ", ".join(get_supported_models())
        raise ValueError(f"Unsupported model '{model_name}'. Supported models: {supported}")
    
    config = MODEL_CONFIGS[model_name]
    library = config.get("library", "clip")
    
    # Check if required library is available
    if library == "transformers" and not TRANSFORMERS_AVAILABLE:
        raise ValueError(f"Model '{model_name}' requires transformers library. Install with: pip install transformers")
    
    return config


def get_model_dim(model_name):
    """Get the embedding dimension for the specified CLIP model"""
    config = get_model_config(model_name)
    return config["embedding_dim"]


def print_supported_models():
    """Print information about all supported models"""
    print("Supported CLIP Models:")
    print("=" * 50)
    for model_name, config in MODEL_CONFIGS.items():
        library = config.get("library", "clip")
        available = True
        reason = ""
        
        if library == "transformers" and not TRANSFORMERS_AVAILABLE:
            available = False
            reason = " (requires transformers)"
        
        status = "✓" if available else "✗"
        print(f"  {status} {model_name}{reason}:")
        print(f"    Description: {config['description']}")
        print(f"    Embedding Dimension: {config['embedding_dim']}")
        print(f"    Library: {library}")
        if library == "transformers" and "hf_model_id" in config:
            print(f"    HuggingFace Model ID: {config['hf_model_id']}")
        print()


def print_all_clip_models():
    """Print information about all models available in PyTorch CLIP"""
    print("All Available PyTorch CLIP Models:")
    print("=" * 60)
    
    try:
        # Get all available models from CLIP
        available_models = clip.available_models()
        print(f"Total models available: {len(available_models)}")
        print(f"Models are cached in: ~/.cache/clip\n")
        
        # Known embedding dimensions for common models (to avoid downloading)
        known_dims = {
            'RN50': 1024,
            'RN101': 512,
            'RN50x4': 640,
            'RN50x16': 768,
            'RN50x64': 1024,
            'ViT-B/32': 512,
            'ViT-B/16': 512,
            'ViT-L/14': 768,
            'ViT-L/14@336px': 768,
        }
        
        for model_name in sorted(available_models):
            print(f"  {model_name}")
            
            # Check if this model is in our supported configs
            if model_name in MODEL_CONFIGS:
                config = MODEL_CONFIGS[model_name]
                print(f"    ✓ Supported in this script")
                print(f"    Description: {config['description']}")
                print(f"    Embedding Dimension: {config['embedding_dim']}")
            else:
                print(f"    ✗ Not yet supported in this script")
                
                # Use known dimensions if available, otherwise indicate unknown
                if model_name in known_dims:
                    embedding_dim = known_dims[model_name]
                    print(f"    Known Embedding Dimension: {embedding_dim}")
                    print(f"    To add support, add this to MODEL_CONFIGS:")
                    print(f'    "{model_name}": {{')
                    print(f'        "model_name": "{model_name}",')
                    print(f'        "embedding_dim": {embedding_dim},')
                    print(f'        "description": "Add description here"')
                    print(f'    }},')
                else:
                    print(f"    Embedding Dimension: Unknown (would need to load model to detect)")
                    print(f"    Note: Run with --detect-dimensions to automatically detect (downloads model)")
            
            print()
            
    except Exception as e:
        print(f"Error getting available models: {e}")


def detect_model_dimensions(model_names=None):
    """Detect embedding dimensions by actually loading models (downloads if needed)"""
    print("Detecting Model Dimensions (this will download models if not cached):")
    print("=" * 70)
    print(f"Cache location: ~/.cache/clip")
    print()
    
    available_models = clip.available_models()
    
    if model_names is None:
        # Detect for unsupported models only
        model_names = [m for m in available_models if m not in MODEL_CONFIGS]
    
    for model_name in model_names:
        if model_name not in available_models:
            print(f"  {model_name}: Not available in PyTorch CLIP")
            continue
            
        print(f"  {model_name}:")
        try:
            print(f"    Loading model (may download ~400MB-1GB)...")
            model, _ = clip.load(model_name, device="cpu")
            
            # Create a test input to determine embedding dimension
            test_text = ["a photo of a cat"]
            test_tokens = clip.tokenize(test_text)
            
            with torch.no_grad():
                test_output = model.encode_text(test_tokens)
                embedding_dim = test_output.shape[-1]
            
            print(f"    ✓ Detected Embedding Dimension: {embedding_dim}")
            print(f"    To add support, add this to MODEL_CONFIGS:")
            print(f'    "{model_name}": {{')
            print(f'        "model_name": "{model_name}",')
            print(f'        "embedding_dim": {embedding_dim},')
            print(f'        "description": "Add description here"')
            print(f'    }},')
            
            # Clean up
            del model
            
        except Exception as e:
            print(f"    ✗ Error detecting dimension: {e}")
        
        print()


def save_tensor_to_bin(tensor: np.ndarray, out_path: str, name: str):
    """Saves a NumPy tensor to a binary file with a header."""
    tensor = tensor.astype(np.float32, copy=False)

    # For 1D vectors, reshape to a 1xN matrix to fit the format
    if tensor.ndim == 1:
        tensor = tensor.reshape(1, -1)

    rows, cols = tensor.shape
    print(f"Exporting {name}: shape = ({rows}, {cols})")

    with open(out_path, 'wb') as f_bin:
        # Header: rows, cols as uint32 little-endian
        f_bin.write(struct.pack('<II', rows, cols))
        f_bin.write(tensor.tobytes(order='C'))

    print(f"Saved {name} to {out_path}")

    # Verification step
    with open(out_path, 'rb') as f_bin:
        rows_read, cols_read = struct.unpack('<II', f_bin.read(8))
        data_read = np.frombuffer(f_bin.read(), dtype=np.float32).reshape(rows_read, cols_read)
    
    assert tensor.shape == data_read.shape, "Shape mismatch during verification!"
    assert np.allclose(tensor, data_read, rtol=1e-5), "Value mismatch during verification!"
    print(f"Verification successful for {out_path}\n")


def load_tensor_from_bin(bin_path: str):
    """Load tensor from binary file with header format: rows, cols (uint32) + data (float32)"""
    if not os.path.exists(bin_path):
        raise FileNotFoundError(f"Binary file not found: {bin_path}")
    
    with open(bin_path, 'rb') as f:
        # Read header: rows, cols as uint32 little-endian
        rows, cols = struct.unpack('<II', f.read(8))
        print(f"Loading tensor from {bin_path}: shape = ({rows}, {cols})")
        
        # Read data as float32
        data = np.frombuffer(f.read(), dtype=np.float32).reshape(rows, cols)
    
    return data


def load_clip_model(model_config):
    """Load CLIP model based on configuration (supports clip and transformers)"""
    library = model_config.get("library", "clip")
    model_name = model_config["model_name"]
    
    if library == "clip":
        print(f"Loading OpenAI CLIP model: {model_name}")
        model, preprocess = clip.load(model_name, device="cpu")
        return model, preprocess, library
    elif library == "transformers":
        if not TRANSFORMERS_AVAILABLE:
            raise ImportError("transformers library is required but not installed. Install with: pip install transformers")
        hf_model_id = model_config.get("hf_model_id", model_name)
        print(f"Loading HuggingFace transformers model: {hf_model_id}")
        model = CLIPModel.from_pretrained(hf_model_id)
        model.eval()
        # HuggingFace doesn't have a preprocess, return None
        return model, None, library
    else:
        raise ValueError(f"Unknown library: {library}")


def tokenize_text(text, model_config, library):
    """Tokenize text based on the library being used"""
    if library == "clip":
        return clip.tokenize(text), None
    elif library == "transformers":
        if not TRANSFORMERS_AVAILABLE:
            raise ImportError("transformers library is required but not installed")
        hf_model_id = model_config.get("hf_model_id", model_config["model_name"])
        tokenizer = CLIPTokenizer.from_pretrained(hf_model_id)
        # HuggingFace returns a dict, we need input_ids and attention_mask
        # IMPORTANT: Set max_length to 77 (CLIP's standard) and pad with 0 to that length
        tokens = tokenizer(text, return_tensors="pt", padding="max_length", max_length=77, truncation=True, pad_to_max_length=True)
        # Manually ensure padding is 0 instead of pad_token_id
        input_ids = tokens['input_ids']
        attention_mask = tokens['attention_mask']
        # Replace pad tokens with 0, but only where attention_mask is 0 (actual padding).
        # This preserves the real EOT token (49407) which shares the same id as pad_token_id.
        if tokenizer.pad_token_id is not None:
            input_ids = torch.where(
                (input_ids == tokenizer.pad_token_id) & (attention_mask == 0),
                torch.tensor(0),
                input_ids
            )
        return input_ids, attention_mask
    else:
        raise ValueError(f"Unknown library: {library}")


def generate_sentence_embedding_from_tokens(tokens: torch.Tensor, embedding_lookup_path: str):
    """Generate sentence embeddings using tokenization and embedding lookup from bin file"""
    print("\n" + "="*60)
    print("GENERATING SENTENCE EMBEDDING FROM TOKENS")
    print("="*60)
    
    # Load embedding lookup table
    embedding_lookup = load_tensor_from_bin(embedding_lookup_path)
    print(f"Embedding lookup table shape: {embedding_lookup.shape}")
    
    # Convert tokens to numpy
    token_ids = tokens.numpy()  # Shape: [batch_size, seq_len]
    print(f"Token IDs shape: {token_ids.shape}")
    print(f"Token IDs: {token_ids[0]}")  # Show first sequence
    
    # Perform embedding lookup
    batch_size, seq_len = token_ids.shape
    embedding_dim = embedding_lookup.shape[1]
    
    sentence_embeddings = np.zeros((batch_size, seq_len, embedding_dim), dtype=np.float32)
    
    for b in range(batch_size):
        for s in range(seq_len):
            token_id = token_ids[b, s]
            sentence_embeddings[b, s] = embedding_lookup[token_id]
    
    print(f"Generated sentence embeddings shape: {sentence_embeddings.shape}")
    print(f"First token embedding (first 10 dims): {sentence_embeddings[0, 0, :10]}")
    
    return sentence_embeddings


def run_onnx_inference(input_embeddings: np.ndarray, onnx_path: str):
    """Run ONNX inference on input embeddings"""
    print("\n" + "="*60)
    print("RUNNING ONNX INFERENCE")
    print("="*60)
    
    # Load ONNX model
    ort_session = ort.InferenceSession(onnx_path)
    
    # Get input/output info
    input_name = ort_session.get_inputs()[0].name
    input_shape = ort_session.get_inputs()[0].shape
    output_name = ort_session.get_outputs()[0].name
    output_shape = ort_session.get_outputs()[0].shape
    
    print(f"ONNX Input name: {input_name}, shape: {input_shape}")
    print(f"ONNX Output name: {output_name}, shape: {output_shape}")
    print(f"Input embeddings shape: {input_embeddings.shape}")
    
    # Run inference
    onnx_outputs = ort_session.run([output_name], {input_name: input_embeddings})
    onnx_result = onnx_outputs[0]
    
    print(f"ONNX output shape: {onnx_result.shape}")
    print(f"ONNX output first 10 values: {onnx_result.flatten()[:10]}")
    
    return onnx_result


def apply_text_projection(last_hidden_state: np.ndarray, weights: np.ndarray, bias: np.ndarray, tokens: torch.Tensor):
    """
    Apply text projection: result = weights * input + bias
    Uses EOT token position for projection
    """
    print("\n" + "="*60)
    print("APPLYING TEXT PROJECTION")
    print("="*60)
    
    print(f"Input shape: {last_hidden_state.shape}")
    print(f"Weights shape: {weights.shape}")
    print(f"Bias shape: {bias.shape}")
    
    # Get EOT token indices
    eot_indices = tokens.argmax(dim=-1).numpy()
    print(f"EOT token indices: {eot_indices}")
    
    # Extract embeddings at EOT positions
    batch_size = last_hidden_state.shape[0]
    input_vectors = np.zeros((batch_size, last_hidden_state.shape[2]), dtype=np.float32)
    
    for b in range(batch_size):
        input_vectors[b] = last_hidden_state[b, eot_indices[b], :]
    
    print(f"Input vectors shape for projection: {input_vectors.shape}")
    print(f"Input vectors first 10 values: {input_vectors[0, :10]}")

    # Matrix multiplication: result = input_vectors @ weights
    projected_embedding = input_vectors @ weights
    
    print(f"After matrix multiplication shape: {projected_embedding.shape}")
    print(f"Projected embedding before bias first 10 values: {projected_embedding[0, :10]}")

    # Add bias
    if bias.ndim == 2 and bias.shape[0] == 1:
        bias = bias.flatten()  # Convert [1, N] to [N]
    
    projected_embedding = projected_embedding + bias
    
    print(f"Final projected embedding shape: {projected_embedding.shape}")
    print(f"Projected embedding first 10 values: {projected_embedding[0, :10]}")
    
    return projected_embedding


def l2_normalize(vec: np.ndarray):
    """L2 normalize the input vector(s)"""
    print("\n" + "="*60)
    print("APPLYING L2 NORMALIZATION")
    print("="*60)
    
    print(f"Input shape: {vec.shape}")
    
    # Calculate L2 norm along the last dimension
    norm = np.linalg.norm(vec, axis=-1, keepdims=True)
    
    # Avoid division by zero
    norm = np.where(norm == 0, 1.0, norm)
    
    # Normalize
    normalized_vec = vec / norm
    
    print(f"L2 norm: {norm.flatten()}")
    print(f"Normalized vector shape: {normalized_vec.shape}")
    print(f"Normalized vector first 10 values: {normalized_vec.flatten()[:10]}")
    
    # Verify normalization
    final_norm = np.linalg.norm(normalized_vec, axis=-1)
    print(f"Final L2 norm (should be ~1.0): {final_norm}")
    
    return normalized_vec


def extract_pytorch_embeddings(tokens: torch.Tensor, model_config: dict, attention_mask=None):
    """Extract embeddings at different stages from PyTorch CLIP model"""
    print("\n" + "="*60)
    print("EXTRACTING PYTORCH EMBEDDINGS AT ALL STAGES")
    print("="*60)
    
    # Load the model
    model, preprocess, library = load_clip_model(model_config)
    
    # Handle different model structures
    if library == "transformers":
        # HuggingFace models have a different structure
        text_model = model.text_model
        text_projection = model.text_projection
        
        # Get dtype from embeddings
        model_dtype = text_model.embeddings.token_embedding.weight.dtype
    else:
        # OpenAI CLIP structure
        text_model = model
        text_projection = model.text_projection
        
        # Get dtype - OpenAI CLIP has model.dtype
        model_dtype = model.dtype
    
    embeddings_after_token = None
    embeddings_before_projection = None
    
    def token_embedding_hook(module, input, output):
        nonlocal embeddings_after_token
        embeddings_after_token = output.detach().clone()
        print(f"[PYTORCH STAGE 1] After token embedding shape: {output.shape}")
        print(f"[PYTORCH STAGE 1] First 5 tokens, first 10 dims:")
        print(output[0, :5, :10].numpy())
    
    # Register hook on token embedding
    if library == "transformers":
        text_model.embeddings.token_embedding.register_forward_hook(token_embedding_hook)
    else:
        model.token_embedding.register_forward_hook(token_embedding_hook)
    
    # Custom encode_text to capture embeddings before projection
    if library == "transformers":
        # HuggingFace implementation
        def modified_encode_text(input_ids, attention_mask=None):
            nonlocal embeddings_before_projection
            
            # Get embeddings (token + positional)
            embeddings_output = text_model.embeddings(input_ids=input_ids)
            
            # Build causal attention mask + padding mask
            # HuggingFace CLIP uses causal masking, so we need to create a triangular mask
            batch_size, seq_length = embeddings_output.shape[0], embeddings_output.shape[1]
            device = embeddings_output.device
            dtype = embeddings_output.dtype
            
            # Create causal mask: lower triangular (1s below diagonal, 0s above)
            # Each token can only attend to itself and previous tokens
            causal_mask = torch.triu(torch.ones(seq_length, seq_length, device=device, dtype=torch.bool), diagonal=1)
            # Invert it: 0s where can attend, 1s where cannot
            causal_mask = ~causal_mask
            
            # Combine with padding mask if provided
            combined_mask = None
            if attention_mask is not None:
                # attention_mask is [batch_size, seq_len] with 1 for real tokens, 0 for padding
                # We need to apply both causal and padding masks
                # First expand causal mask to batch dimension: [1, seq_len, seq_len] -> [batch_size, seq_len, seq_len]
                causal_mask_expanded = causal_mask.unsqueeze(0).expand(batch_size, -1, -1)
                
                # Create padding mask as 2D: [batch_size, seq_len]
                # Convert to attention format: where attention_mask=1 means "attend", we want 0 in the mask
                padding_mask_2d = attention_mask.unsqueeze(1).unsqueeze(2)  # [batch_size, 1, 1, seq_len]
                
                # Create the full 4D mask [batch_size, 1, seq_len, seq_len]
                # True = attend, False = mask out
                combined_mask = causal_mask_expanded.unsqueeze(1) & padding_mask_2d.bool()
                # Convert to attention scores format: 0 where attend, -inf where mask
                combined_mask = torch.where(combined_mask, torch.tensor(0.0, dtype=dtype, device=device), 
                                           torch.tensor(torch.finfo(dtype).min, dtype=dtype, device=device))
            else:
                # Only causal mask, no padding
                causal_mask_expanded = causal_mask.unsqueeze(0).expand(batch_size, -1, -1).unsqueeze(1)
                combined_mask = torch.where(causal_mask_expanded, torch.tensor(0.0, dtype=dtype, device=device),
                                           torch.tensor(torch.finfo(dtype).min, dtype=dtype, device=device))
            
            # Pass through encoder with combined mask
            encoder_outputs = text_model.encoder(
                inputs_embeds=embeddings_output,
                attention_mask=combined_mask
            )
            
            # Extract last hidden state from different return types
            if hasattr(encoder_outputs, 'last_hidden_state'):
                # It's a ModelOutput object
                last_hidden_state = encoder_outputs.last_hidden_state
            elif isinstance(encoder_outputs, tuple):
                # It's a tuple
                last_hidden_state = encoder_outputs[0]
            else:
                # It's a tensor
                last_hidden_state = encoder_outputs
            
            # Apply final layer norm
            last_hidden_state = text_model.final_layer_norm(last_hidden_state)
            
            # Capture embeddings before projection
            embeddings_before_projection = last_hidden_state.detach().clone()
            print(f"\n[PYTORCH STAGE 2] Before text projection shape: {last_hidden_state.shape}")
            print(f"[PYTORCH STAGE 2] First 10 tokens, first 10 dims:")
            print(last_hidden_state[0, :10, :10].numpy())
            
            # Take features from the eot embedding (last token for HF)
            # HuggingFace uses EOS token which is at the end
            eot_indices = input_ids.argmax(dim=-1)
            print(f"[PYTORCH STAGE 2] EOT token indices: {eot_indices}")
            
            pooled_output = last_hidden_state[torch.arange(last_hidden_state.shape[0]), eot_indices]
            
            # Apply text projection
            if text_projection is not None:
                # Check if it's a Linear layer or a weight matrix
                if hasattr(text_projection, 'weight'):
                    # It's a nn.Linear layer
                    pooled_output = text_projection(pooled_output)
                else:
                    # It's a weight matrix
                    pooled_output = pooled_output @ text_projection
            
            return pooled_output
        
        # Run forward pass
        with torch.no_grad():
            pytorch_after_projection = modified_encode_text(tokens, attention_mask)
            pytorch_after_l2_norm = pytorch_after_projection / pytorch_after_projection.norm(dim=-1, keepdim=True)
    else:
        # OpenAI CLIP / OpenCLIP implementation
        original_encode_text = model.encode_text
        
        def modified_encode_text(text):
            nonlocal embeddings_before_projection
            
            # Get token embeddings
            x = model.token_embedding(text).type(model_dtype)  # [batch_size, n_ctx, d_model]
            
            # Add positional encoding
            x = x + model.positional_embedding.type(model_dtype)
            x = x.permute(1, 0, 2)  # NLD -> LND
            
            # Pass through transformer
            x = model.transformer(x)
            x = x.permute(1, 0, 2)  # LND -> NLD
            
            # Apply layer norm
            x = model.ln_final(x).type(model_dtype)
            
            # Capture embeddings before projection
            embeddings_before_projection = x.detach().clone()
            print(f"\n[PYTORCH STAGE 2] Before text projection shape: {x.shape}")
            print(f"[PYTORCH STAGE 2] First 10 tokens, first 10 dims:")
            print(x[0, :10, :10].numpy())
            
            # Take features from the eot embedding
            eot_indices = text.argmax(dim=-1)
            print(f"[PYTORCH STAGE 2] EOT token indices: {eot_indices}")
            x = x[torch.arange(x.shape[0]), eot_indices] @ model.text_projection
            
            return x
        
        # Replace encode_text temporarily
        model.encode_text = modified_encode_text
        
        # Run forward pass
        with torch.no_grad():
            pytorch_after_projection = model.encode_text(tokens)
            pytorch_after_l2_norm = pytorch_after_projection / pytorch_after_projection.norm(dim=-1, keepdim=True)
        
        # Restore original method
        model.encode_text = original_encode_text
    
    print(f"\n[PYTORCH STAGE 3] After projection shape: {pytorch_after_projection.shape}")
    print(f"[PYTORCH STAGE 3] After projection first 10 values: {pytorch_after_projection[0, :10].numpy()}")
    
    print(f"\n[PYTORCH STAGE 4] After L2 normalization shape: {pytorch_after_l2_norm.shape}")
    print(f"[PYTORCH STAGE 4] After L2 normalization first 10 values: {pytorch_after_l2_norm[0, :10].numpy()}")
    
    return (embeddings_after_token.numpy(), 
            embeddings_before_projection.numpy(), 
            pytorch_after_projection.numpy(), 
            pytorch_after_l2_norm.numpy())


def compare_embeddings(pytorch_data, hailo_data, stage_name):
    """Compare embeddings between PyTorch and Hailo implementations"""
    print(f"\n[COMPARISON] {stage_name}")
    print("-" * 40)
    
    if pytorch_data.shape != hailo_data.shape:
        print(f"Shape mismatch: PyTorch {pytorch_data.shape}, Hailo {hailo_data.shape}")
        return False
    
    diff = np.abs(pytorch_data - hailo_data)
    max_diff = np.max(diff)
    mean_diff = np.mean(diff)
    median_diff = np.median(diff)
    
    # Find location of max difference
    max_diff_idx = np.unravel_index(np.argmax(diff), diff.shape)
    
    print(f"Max difference: {max_diff} at position {max_diff_idx}")
    print(f"Mean difference: {mean_diff}")
    print(f"Median difference: {median_diff}")
    
    # Calculate percentage of values that match closely
    close_matches = np.sum(diff < 1e-4)
    total_elements = diff.size
    match_percentage = (close_matches / total_elements) * 100
    print(f"Values matching within 1e-4: {close_matches}/{total_elements} ({match_percentage:.2f}%)")
    
    if np.allclose(pytorch_data, hailo_data, rtol=1e-4, atol=1e-5):
        print(f"✓ {stage_name} matches!")
        return True
    else:
        print(f"✗ {stage_name} does not match.")
        print("First 10 values comparison:")
        print(f"PyTorch: {pytorch_data.flatten()[:10]}")
        print(f"Hailo:   {hailo_data.flatten()[:10]}")
        print(f"Diff:    {diff.flatten()[:10]}")
        
        # Additional diagnostics
        if max_diff_idx:
            print(f"\nMax difference location analysis:")
            print(f"  Position: {max_diff_idx}")
            print(f"  PyTorch value: {pytorch_data[max_diff_idx]}")
            print(f"  Hailo value: {hailo_data[max_diff_idx]}")
        
        return False


def main():
    parser = argparse.ArgumentParser(description='CLIP Text Encoder Full Test - PyTorch vs Hailo ONNX comparison')
    parser.add_argument('--prompt', type=str, default='a photo of a cat',
                       help='Text prompt to encode (default: "a photo of a cat")')
    parser.add_argument('--bin-folder', type=str,
                       help='Path to folder containing bin files (embedding_lookup.bin, projection_matrix_weights.bin, projection_matrix_bias.bin)')
    parser.add_argument('--sentence-embedding-path', type=str,
                       help='Optional path to sentence embedding .npy file (if not provided, will generate from tokenization)')
    parser.add_argument('--hailo-onnx-path', type=str,
                       help='Path to Hailo ONNX model file')
    parser.add_argument('--list-models', action='store_true',
                       help='List all supported CLIP models and exit')
    parser.add_argument('--list-all-clip-models', action='store_true',
                       help='List all available PyTorch CLIP models (including unsupported ones) and exit')
    parser.add_argument('--list-all', action='store_true',
                       help='List both supported models and all available PyTorch CLIP models and exit')
    parser.add_argument('--detect-dimensions', action='store_true',
                       help='Detect embedding dimensions for unsupported models (downloads models if needed) and exit')
    parser.add_argument('--detect-model', type=str, 
                       help='Detect embedding dimension for a specific model (downloads if needed) and exit')
    parser.add_argument('--model', type=str, default='ViT-B/32',
                       choices=get_supported_models(),
                       help=f'CLIP model to use (default: ViT-B/32). Supported: {", ".join(get_supported_models())}')
    parser.add_argument('--output-dir', type=str, default='.',
                       help='Output directory for .npy files (default: current directory)')
    
    args = parser.parse_args()
    
    # Handle list models options
    if args.list_models:
        print_supported_models()
        sys.exit(0)
    
    if args.list_all_clip_models:
        print_all_clip_models()
        sys.exit(0)
    
    if args.list_all:
        print_supported_models()
        print("\n")
        print_all_clip_models()
        sys.exit(0)
    
    if args.detect_dimensions:
        detect_model_dimensions()
        sys.exit(0)
    
    if args.detect_model:
        detect_model_dimensions([args.detect_model])
        sys.exit(0)
    
    # Validate required arguments when not listing models
    if not args.bin_folder:
        parser.error("--bin-folder is required (unless using listing options)")
    if not args.hailo_onnx_path:
        parser.error("--hailo-onnx-path is required (unless using listing options)")
    
    # Validate required bin files
    required_bins = ['embedding_lookup.bin', 'projection_matrix_weights.bin', 'projection_matrix_bias.bin']
    bin_paths = {}
    
    for bin_file in required_bins:
        bin_path = os.path.join(args.bin_folder, bin_file)
        if not os.path.exists(bin_path):
            print(f"Error: Required bin file not found: {bin_path}")
            sys.exit(1)
        bin_paths[bin_file.replace('.bin', '')] = bin_path
    
    # Validate ONNX model
    if not os.path.exists(args.hailo_onnx_path):
        print(f"Error: ONNX model not found: {args.hailo_onnx_path}")
        sys.exit(1)
    
    # Create output directory
    os.makedirs(args.output_dir, exist_ok=True)
    
    # Get model configuration and display info
    model_config = get_model_config(args.model)
    library = model_config.get("library", "clip")
    print(f"Using model: {model_config['model_name']}")
    print(f"Description: {model_config['description']}")
    print(f"Library: {library}")
    if library == "open_clip" and "pretrained" in model_config:
        print(f"Pretrained weights: {model_config['pretrained']}")
    if library == "transformers" and "hf_model_id" in model_config:
        print(f"HuggingFace Model ID: {model_config['hf_model_id']}")
    print(f"Expected embedding dimension: {model_config['embedding_dim']}")
    print(f"Prompt: '{args.prompt}'")
    
    # Tokenize the prompt
    text = [args.prompt]
    tokens, attention_mask = tokenize_text(text, model_config, library)
    print(f"Tokens: {tokens}")
    print(f"Tokens shape: {tokens.shape}")
    if attention_mask is not None:
        print(f"Attention mask: {attention_mask}")
        print(f"Attention mask shape: {attention_mask.shape}")
    
    try:
        # Step 1: Get PyTorch embeddings at all stages
        (pytorch_sentence_emb, 
         pytorch_before_proj, 
         pytorch_after_proj, 
         pytorch_after_l2) = extract_pytorch_embeddings(tokens, model_config, attention_mask)
        
        # Save PyTorch embeddings
        np.save(os.path.join(args.output_dir, "pytorch_sentence_embedding.npy"), pytorch_sentence_emb)
        np.save(os.path.join(args.output_dir, "pytorch_before_projection.npy"), pytorch_before_proj)
        np.save(os.path.join(args.output_dir, "pytorch_after_projection.npy"), pytorch_after_proj)
        np.save(os.path.join(args.output_dir, "pytorch_after_l2_norm.npy"), pytorch_after_l2)
        
        # Step 2: Generate or load sentence embeddings for Hailo
        if args.sentence_embedding_path:
            print(f"\nLoading sentence embedding from: {args.sentence_embedding_path}")
            hailo_sentence_emb = np.load(args.sentence_embedding_path)
        else:
            hailo_sentence_emb = generate_sentence_embedding_from_tokens(tokens, bin_paths['embedding_lookup'])
        
        # Save Hailo sentence embedding
        np.save(os.path.join(args.output_dir, "hailo_sentence_embedding.npy"), hailo_sentence_emb)
        
        # Step 3: Run Hailo ONNX inference
        hailo_before_proj = run_onnx_inference(hailo_sentence_emb, args.hailo_onnx_path)
        np.save(os.path.join(args.output_dir, "hailo_before_projection.npy"), hailo_before_proj)
        
        # Step 4: Apply text projection
        projection_weights = load_tensor_from_bin(bin_paths['projection_matrix_weights'])
        projection_bias = load_tensor_from_bin(bin_paths['projection_matrix_bias'])
        
        # Validate projection matrix dimensions match the model
        expected_dim = model_config['embedding_dim']
        if projection_weights.shape[0] != expected_dim or projection_weights.shape[1] != expected_dim:
            print(f"\n⚠️  WARNING: Projection matrix shape {projection_weights.shape} does not match expected [{expected_dim}, {expected_dim}] for model {args.model}")
            print(f"   This will cause mismatched results! Make sure your bin files are for the correct model.")
        
        hailo_after_proj = apply_text_projection(hailo_before_proj, projection_weights, projection_bias, tokens)
        np.save(os.path.join(args.output_dir, "hailo_after_projection.npy"), hailo_after_proj)
        
        # Step 5: Apply L2 normalization
        hailo_after_l2 = l2_normalize(hailo_after_proj)
        np.save(os.path.join(args.output_dir, "hailo_after_l2_norm.npy"), hailo_after_l2)
        
        # Step 6: Compare all stages
        print("\n" + "="*60)
        print("COMPREHENSIVE COMPARISON RESULTS")
        print("="*60)
        
        stage1_match = compare_embeddings(pytorch_sentence_emb, hailo_sentence_emb, "Sentence Embedding")
        stage2_match = compare_embeddings(pytorch_before_proj, hailo_before_proj, "Before Projection")
        stage3_match = compare_embeddings(pytorch_after_proj, hailo_after_proj, "After Projection")
        stage4_match = compare_embeddings(pytorch_after_l2, hailo_after_l2, "After L2 Normalization")
        
        # Summary
        print("\n" + "="*60)
        print("FINAL SUMMARY")
        print("="*60)
        print(f"Sentence Embedding:      {'✓' if stage1_match else '✗'}")
        print(f"Before Projection:       {'✓' if stage2_match else '✗'}")
        print(f"After Projection:        {'✓' if stage3_match else '✗'}")
        print(f"After L2 Normalization:  {'✓' if stage4_match else '✗'}")
        
        if all([stage1_match, stage2_match, stage3_match, stage4_match]):
            print("\n🎉 All stages match! PyTorch and Hailo implementations are equivalent.")
        else:
            print("\n⚠️  Some stages don't match BUT if last stage match (After L2 Normalization), your ONNX model is likely correct. Check the detailed below and comparisons above.")
            
            # Provide diagnostic guidance
            print("\n" + "="*60)
            print("DIAGNOSTIC GUIDANCE")
            print("="*60)
            
            if stage1_match and not stage2_match:
                print("✓ Sentence embeddings match perfectly")
                print("✗ ONNX transformer output doesn't match (BUT see note below)")
                print("\n🔍 IMPORTANT NOTE - This may be acceptable:")
                print("   CLIP uses EOT (end-of-text) token pooling, meaning only the embedding at")
                print("   the EOT token position is used for the final output. Differences at other")
                print("   token positions (especially padding positions) are irrelevant.")
                print("\n   ✓ If 'After Projection' and 'After L2 Normalization' both match 100%,")
                print("     your ONNX model is CORRECT for production use!")
                print("\nLikely causes of 'Before Projection' mismatch:")
                print("1. ONNX model was exported WITHOUT causal masking (bidirectional attention)")
                print("2. Different masking strategy during ONNX export")
                print("3. Differences in attention mechanism implementation")
                print("4. Quantization/optimization that only affects intermediate values")
                print(f"\n   Your model: {model_config['model_name']} (pretrained={model_config.get('pretrained', 'N/A')})")
                
            if not stage1_match:
                print("✗ Sentence embeddings don't match")
                print("\nLikely causes:")
                print("1. embedding_lookup.bin is from a different model")
                print("2. Tokenization mismatch between libraries")
                
            if stage2_match and not stage3_match:
                print("✓ ONNX transformer output matches")
                print("✗ After projection doesn't match")
                print("\nLikely causes:")
                print("1. projection_matrix_weights.bin or projection_matrix_bias.bin is from a different model")
                print("2. EOT token position is incorrect")
            
            print("\n⚠️  Note: If using your own sentence embedding .npy file, ensure the prompt matches!")
        
        print(f"\nAll results saved to: {args.output_dir}")
        print("Files created:")
        print("  PyTorch results:")
        print("    - pytorch_sentence_embedding.npy")
        print("    - pytorch_before_projection.npy")
        print("    - pytorch_after_projection.npy")
        print("    - pytorch_after_l2_norm.npy")
        print("  Hailo results:")
        print("    - hailo_sentence_embedding.npy")
        print("    - hailo_before_projection.npy")
        print("    - hailo_after_projection.npy")
        print("    - hailo_after_l2_norm.npy")
        
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
