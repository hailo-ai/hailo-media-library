import onnx
import numpy as np
import struct
import sys
import os
from typing import List, Tuple, Optional

def list_model_tensors(onnx_path: str):
    """Helper function to list all tensor names in the ONNX model"""
    model = onnx.load(onnx_path)
    print(f"All tensors in {onnx_path}:")
    print("-" * 70)
    for i, initializer in enumerate(model.graph.initializer):
        shape = [dim for dim in initializer.dims]
        print(f"  {i:3d}: {initializer.name} - shape: {shape}")
    print("-" * 70)

def find_token_embedding_tensor(model):
    """
    Find the token embedding tensor in ONNX model.
    Common names for token embedding tensors in CLIP models.
    """
    common_names = [
        'text_model.embeddings.token_embedding.weight',
        'embeddings.token_embedding.weight', 
        'token_embedding.weight',
        'text.token_embedding.weight',
        'clip.text_model.embeddings.token_embedding.weight',
        'text_encoder.embeddings.token_embedding.weight'
    ]
    
    # Get all initializer names
    initializer_names = [init.name for init in model.graph.initializer]
    
    # Try common names first
    for name in common_names:
        if name in initializer_names:
            return name
    
    # If not found, look for patterns containing "token_embedding"
    token_embedding_names = [name for name in initializer_names if 'token_embedding' in name.lower()]
    
    if token_embedding_names:
        print(f"Found token embedding candidates: {token_embedding_names}")
        return token_embedding_names[0]  # Return first match
    
    return None

def find_text_projection_tensors(model):
    """
    Find the text projection tensors in ONNX model.
    Common names for text projection tensors in CLIP models.
    """
    # Get all initializer names
    initializer_names = [init.name for init in model.graph.initializer]
    
    # Common patterns for text projection weights and bias
    weight_patterns = [
        'clip_model.text_projection',  # Most common case - single tensor without .weight suffix
        'text_projection.weight',
        'clip.text_projection.weight', 
        'text_model.text_projection.weight',
        'text_encoder.text_projection.weight',
        'model.text_projection.weight',
        'clip_model.text_projection.weight',
        'text_projection',  # Sometimes just the base name
        'clip.text_projection',
        'text_model.text_projection'
    ]
    
    bias_patterns = [
        'clip_model.text_projection.bias',
        'text_projection.bias',
        'clip.text_projection.bias',
        'text_model.text_projection.bias', 
        'text_encoder.text_projection.bias',
        'model.text_projection.bias'
    ]
    
    # Find weight tensor
    weight_name = None
    for pattern in weight_patterns:
        if pattern in initializer_names:
            weight_name = pattern
            break
    
    # If not found with exact match, search for patterns
    if weight_name is None:
        projection_weights = [name for name in initializer_names if 'text_projection' in name.lower() and 'weight' in name.lower()]
        if projection_weights:
            weight_name = projection_weights[0]
    
    # Find bias tensor (may not exist)
    bias_name = None
    for pattern in bias_patterns:
        if pattern in initializer_names:
            bias_name = pattern
            break
    
    # If not found with exact match, search for patterns
    if bias_name is None:
        projection_biases = [name for name in initializer_names if 'text_projection' in name.lower() and 'bias' in name.lower()]
        if projection_biases:
            bias_name = projection_biases[0]
    
    return weight_name, bias_name

def extract_tensor_from_model(model, tensor_name: str) -> Optional[np.ndarray]:
    """Extract a tensor by name from ONNX model and convert to numpy array."""
    if tensor_name is None:
        return None
        
    for initializer in model.graph.initializer:
        if initializer.name == tensor_name:
            return onnx.numpy_helper.to_array(initializer)
    
    return None

def save_tensor_to_bin(tensor: np.ndarray, out_path: str, name: str):
    """Saves a NumPy tensor to a binary file with a header."""
    tensor = tensor.astype(np.float32, copy=False)

    # For 1D vectors, reshape to a 1xN matrix to fit the format
    if tensor.ndim == 1:
        tensor = tensor.reshape(1, -1)

    rows, cols = tensor.shape
    print(f"Saving {name}: shape = ({rows}, {cols}) -> {out_path}")

    with open(out_path, 'wb') as f_bin:
        # Header: rows, cols as uint32 little-endian
        f_bin.write(struct.pack('<II', rows, cols))
        f_bin.write(tensor.tobytes(order='C'))

    # Verification step
    with open(out_path, 'rb') as f_bin:
        rows_read, cols_read = struct.unpack('<II', f_bin.read(8))
        data_read = np.frombuffer(f_bin.read(), dtype=np.float32).reshape(rows_read, cols_read)
    
    assert tensor.shape == data_read.shape, f"Shape mismatch during verification for {name}!"
    assert np.allclose(tensor, data_read, rtol=1e-5), f"Value mismatch during verification for {name}!"
    print(f"✓ Verification successful for {name}")

def extract_clip_tensors(onnx_path: str, output_folder: str, 
                        embedding_tensor_name: str = None,
                        weight_tensor_name: str = None, 
                        bias_tensor_name: str = None,
                        list_tensors: bool = False):
    """
    Extract token embeddings and text projection from ONNX CLIP model to binary format.
    
    Args:
        onnx_path: Path to ONNX model file
        output_folder: Folder where the 3 binary files will be saved
        embedding_tensor_name: Specific embedding tensor name (optional, will auto-detect if not provided)
        weight_tensor_name: Specific weight tensor name (optional, will auto-detect if not provided)
        bias_tensor_name: Specific bias tensor name (optional, will auto-detect if not provided)
        list_tensors: If True, just list all tensors and exit
    """
    # Load ONNX model
    print(f"Loading ONNX model from: {onnx_path}")
    try:
        model = onnx.load(onnx_path)
    except Exception as e:
        print(f"Error loading ONNX model: {e}")
        sys.exit(1)
    
    if list_tensors:
        list_model_tensors(onnx_path)
        return
    
    # Create output folder if it doesn't exist
    os.makedirs(output_folder, exist_ok=True)
    print(f"Output folder: {output_folder}")
    
    # Define output file paths
    embedding_path = os.path.join(output_folder, "embedding_lookup.bin")
    weights_path = os.path.join(output_folder, "projection_matrix_weights.bin")
    bias_path = os.path.join(output_folder, "projection_matrix_bias.bin")
    
    print("\n" + "="*60)
    print("EXTRACTING TOKEN EMBEDDINGS")
    print("="*60)
    
    # Find and extract token embedding tensor
    if embedding_tensor_name is None:
        embedding_tensor_name = find_token_embedding_tensor(model)
    
    if embedding_tensor_name is None:
        print("Error: Could not find token embedding tensor.")
        print("Available tensor names:")
        initializer_names = [init.name for init in model.graph.initializer]
        for name in initializer_names:
            print(f"  {name}")
        print("\nTry using --list to see all tensors or specify --embedding-name")
        sys.exit(1)
    
    print(f"Using token embedding tensor: {embedding_tensor_name}")
    
    # Extract the embedding tensor
    token_embedding = extract_tensor_from_model(model, embedding_tensor_name)
    if token_embedding is None:
        print(f"Error: Tensor '{embedding_tensor_name}' not found in model")
        sys.exit(1)
    
    print(f"Found token embeddings: shape = {token_embedding.shape}")
    
    # Save token embeddings
    save_tensor_to_bin(token_embedding, embedding_path, "Token Embeddings")
    
    print("\n" + "="*60)
    print("EXTRACTING TEXT PROJECTION")
    print("="*60)
    
    # Find text projection tensors
    if weight_tensor_name is None or bias_tensor_name is None:
        auto_weight_name, auto_bias_name = find_text_projection_tensors(model)
        if weight_tensor_name is None:
            weight_tensor_name = auto_weight_name
        if bias_tensor_name is None:
            bias_tensor_name = auto_bias_name
    
    print(f"Using weight tensor: {weight_tensor_name}")
    print(f"Using bias tensor: {bias_tensor_name}")
    
    # Extract weight tensor
    weights = extract_tensor_from_model(model, weight_tensor_name)
    if weights is None:
        print(f"Error: Weight tensor '{weight_tensor_name}' not found in model")
        print("\nTry using --list to see available tensors")
        sys.exit(1)
    
    print(f"Found projection weights: shape = {weights.shape}")
    
    # Extract bias tensor (may be None for some models)
    bias = extract_tensor_from_model(model, bias_tensor_name)
    if bias is None:
        if bias_tensor_name is not None:
            print(f"Warning: Bias tensor '{bias_tensor_name}' not found, creating zero bias")
        else:
            print("Warning: No bias tensor found, creating zero bias")
        
        # Create zero bias if none exists
        if len(weights.shape) == 2:
            bias = np.zeros(weights.shape[1], dtype=np.float32)
            print(f"Created zero bias with shape {bias.shape}")
        else:
            print(f"Error: Cannot create bias for weights with shape {weights.shape}")
            sys.exit(1)
    else:
        print(f"Found projection bias: shape = {bias.shape}")
    
    # Validate dimensions
    if len(weights.shape) != 2:
        print(f"Error: Expected 2D weight tensor, got shape {weights.shape}")
        sys.exit(1)
    
    if len(bias.shape) != 1:
        print(f"Error: Expected 1D bias tensor, got shape {bias.shape}")
        sys.exit(1)
    
    if weights.shape[1] != bias.shape[0]:
        print(f"Error: Dimension mismatch - weights output dim {weights.shape[1]} != bias dim {bias.shape[0]}")
        sys.exit(1)
    
    # Save tensors to binary format
    save_tensor_to_bin(weights, weights_path, "Projection Weights")
    save_tensor_to_bin(bias, bias_path, "Projection Bias")
    
    print("\n" + "="*60)
    print("EXTRACTION COMPLETE")
    print("="*60)
    print(f"✓ Token embeddings saved to: {embedding_path}")
    print(f"✓ Projection weights saved to: {weights_path}")
    print(f"✓ Projection bias saved to: {bias_path}")
    
    # Summary
    print(f"\nExtracted tensors:")
    print(f"  Token embedding: {embedding_tensor_name} - {token_embedding.shape}")
    print(f"  Projection weight: {weight_tensor_name} - {weights.shape}")
    print(f"  Projection bias: {bias_tensor_name if bias_tensor_name else 'created zero bias'} - {bias.shape}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("CLIP Tensor Extractor")
        print("="*50)
        print("Usage:")
        print("  List tensors:")
        print("    python extract_clip_tensors.py model.onnx --list")
        print()
        print("  Extract tensors:")
        print("    python extract_clip_tensors.py model.onnx output_folder")
        print()
        print("  Extract with custom tensor names:")
        print("    python extract_clip_tensors.py model.onnx output_folder \\")
        print("           --embedding-name <name> --weight-name <name> --bias-name <name>")
        print()
        print("Output files:")
        print("  - embedding_lookup.bin")
        print("  - projection_matrix_weights.bin") 
        print("  - projection_matrix_bias.bin")
        sys.exit(1)

    onnx_file = sys.argv[1]
    
    # Check if listing tensors
    if len(sys.argv) == 3 and sys.argv[2] == "--list":
        extract_clip_tensors(onnx_file, "", list_tensors=True)
        sys.exit(0)
    
    if len(sys.argv) < 3:
        print("Error: Output folder not specified")
        print("Usage: python extract_clip_tensors.py model.onnx output_folder")
        sys.exit(1)
    
    output_folder = sys.argv[2]
    
    # Parse optional tensor name arguments
    embedding_tensor_name = None
    weight_tensor_name = None
    bias_tensor_name = None
    
    i = 3
    while i < len(sys.argv):
        if sys.argv[i] == "--embedding-name" and i + 1 < len(sys.argv):
            embedding_tensor_name = sys.argv[i + 1]
            i += 2
        elif sys.argv[i] == "--weight-name" and i + 1 < len(sys.argv):
            weight_tensor_name = sys.argv[i + 1]
            i += 2
        elif sys.argv[i] == "--bias-name" and i + 1 < len(sys.argv):
            bias_tensor_name = sys.argv[i + 1]
            i += 2
        else:
            print(f"Warning: Unknown argument '{sys.argv[i]}'")
            i += 1
    
    try:
        extract_clip_tensors(onnx_file, output_folder, embedding_tensor_name, 
                           weight_tensor_name, bias_tensor_name)
    except Exception as e:
        print(f"Error: {e}")
        print("\nTry using --list to see available tensors:")
        print(f"python {sys.argv[0]} {onnx_file} --list")
        sys.exit(1)

########################## USAGE EXAMPLES ############################################
#
# Basic usage (auto-detect all tensors):
# python extract_clip_tensors.py clip-vit-base-patch32.onnx ./output
#
# List all available tensors in model:
# python extract_clip_tensors.py clip-vit-base-patch32.onnx --list
#
# Specify custom tensor names:
# python extract_clip_tensors.py model.onnx ./output \
#        --embedding-name text_model.embeddings.token_embedding.weight \
#        --weight-name text_projection.weight \
#        --bias-name text_projection.bias
#
# Examples:
# python extract_clip_tensors.py clip-vit-base-patch32.onnx ./clip_b32_tensors
# python extract_clip_tensors.py clip-vit-large-patch14.onnx ./clip_l14_tensors
