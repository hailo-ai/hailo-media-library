#!/usr/bin/env python3
"""
CLIP Text Encoder Full Test Script

This script performs a comprehensive comparison between CLIP PyTorch and Hailo ONNX implementations:
1. Allows user to enter custom prompt (default: "a photo of a cat")
2. Generates sentence embeddings using tokenization + embedding lookup from bin files
3. Compares outputs at each stage: sentence embedding, before projection, after projection, after L2 normalization
4. Saves intermediate results as .npy files

Requirements:
- Bin files in specified folder: embedding_lookup.bin, projection_matrix_weights.bin, projection_matrix_bias.bin
- Optional sentence embedding .npy file (if not provided, generates from tokenization)

Usage Example:

- Classic Compare Example:
python clip_text_encoder_full_test.py --prompt "a photo of a cat" --bin-folder ./bin_files --hailo-onnx-path ./clip_text_encoder.onnx --model ViT-B/32 --output-dir ./output

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


# Model configuration for supported CLIP variants
# To add a new model, simply add an entry here with:
# - model_name: The exact name used by the clip.load() function
# - embedding_dim: The dimension of the text embeddings for this model
# - description: A human-readable description of the model
MODEL_CONFIGS = {
    "ViT-B/32": {
        "model_name": "ViT-B/32",
        "embedding_dim": 512,
        "description": "Vision Transformer Base with 32x32 patches"
    },
    "RN50x4": {
        "model_name": "RN50x4", 
        "embedding_dim": 640,
        "description": "ResNet-50 with 4x width multiplier"
    },
    # Example of how to add more models:
    # "ViT-L/14": {
    #     "model_name": "ViT-L/14",
    #     "embedding_dim": 768,
    #     "description": "Vision Transformer Large with 14x14 patches"
    # },
    # "ViT-L/14@336px": {
    #     "model_name": "ViT-L/14@336px",
    #     "embedding_dim": 768,
    #     "description": "Vision Transformer Large with 14x14 patches at 336px resolution"
    # },
    # "RN50": {
    #     "model_name": "RN50",
    #     "embedding_dim": 1024,
    #     "description": "ResNet-50"
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
    return MODEL_CONFIGS[model_name]


def get_model_dim(model_name):
    """Get the embedding dimension for the specified CLIP model"""
    config = get_model_config(model_name)
    return config["embedding_dim"]


def print_supported_models():
    """Print information about all supported models"""
    print("Supported CLIP Models:")
    print("=" * 50)
    for model_name, config in MODEL_CONFIGS.items():
        print(f"  {model_name}:")
        print(f"    Description: {config['description']}")
        print(f"    Embedding Dimension: {config['embedding_dim']}")
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


def extract_pytorch_embeddings(tokens: torch.Tensor, model_name: str):
    """Extract embeddings at different stages from PyTorch CLIP model"""
    print("\n" + "="*60)
    print("EXTRACTING PYTORCH EMBEDDINGS AT ALL STAGES")
    print("="*60)
    
    # Load the model
    model, preprocess = clip.load(model_name, device="cpu")
    
    embeddings_after_token = None
    embeddings_before_projection = None
    
    def token_embedding_hook(module, input, output):
        nonlocal embeddings_after_token
        embeddings_after_token = output.detach().clone()
        print(f"[PYTORCH STAGE 1] After token embedding shape: {output.shape}")
        print(f"[PYTORCH STAGE 1] First 5 tokens, first 10 dims:")
        print(output[0, :5, :10].numpy())
    
    # Register hook on token embedding
    model.token_embedding.register_forward_hook(token_embedding_hook)
    
    # Custom encode_text to capture embeddings before projection
    original_encode_text = model.encode_text
    
    def modified_encode_text(text):
        nonlocal embeddings_before_projection
        
        # Get token embeddings
        x = model.token_embedding(text).type(model.dtype)  # [batch_size, n_ctx, d_model]
        
        # Add positional encoding
        x = x + model.positional_embedding.type(model.dtype)
        x = x.permute(1, 0, 2)  # NLD -> LND
        
        # Pass through transformer
        x = model.transformer(x)
        x = x.permute(1, 0, 2)  # LND -> NLD
        
        # Apply layer norm
        x = model.ln_final(x).type(model.dtype)
        
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
    
    print(f"Max difference: {max_diff}")
    print(f"Mean difference: {mean_diff}")
    
    if np.allclose(pytorch_data, hailo_data, rtol=1e-4, atol=1e-5):
        print(f"✓ {stage_name} matches!")
        return True
    else:
        print(f"✗ {stage_name} does not match.")
        print("First 10 values comparison:")
        print(f"PyTorch: {pytorch_data.flatten()[:10]}")
        print(f"Hailo:   {hailo_data.flatten()[:10]}")
        print(f"Diff:    {diff.flatten()[:10]}")
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
    print(f"Using model: {model_config['model_name']}")
    print(f"Description: {model_config['description']}")
    print(f"Expected embedding dimension: {model_config['embedding_dim']}")
    print(f"Prompt: '{args.prompt}'")
    
    # Tokenize the prompt
    text = [args.prompt]
    tokens = clip.tokenize(text)
    print(f"Tokens: {tokens}")
    print(f"Tokens shape: {tokens.shape}")
    
    try:
        # Step 1: Get PyTorch embeddings at all stages
        (pytorch_sentence_emb, 
         pytorch_before_proj, 
         pytorch_after_proj, 
         pytorch_after_l2) = extract_pytorch_embeddings(tokens, args.model)
        
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
            print("\n⚠️  Some stages don't match. Check the detailed comparisons above.")
            print("\n⚠️  Please keep in mind if you are USING your own sentence embedding npy file, make sure the prompt matches since your own sentence embedding is a result of specific prompt!")
        
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
