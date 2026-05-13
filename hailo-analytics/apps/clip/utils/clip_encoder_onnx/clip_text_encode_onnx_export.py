import torch
import torch.nn as nn
import os
import argparse
import sys

# Try importing both CLIP libraries
try:
    import clip
    OPENAI_CLIP_AVAILABLE = True
except ImportError:
    OPENAI_CLIP_AVAILABLE = False
    print("⚠️  OpenAI CLIP not available. Install with: pip install git+https://github.com/openai/CLIP.git")

try:
    import open_clip
    OPEN_CLIP_AVAILABLE = True
except ImportError:
    OPEN_CLIP_AVAILABLE = False
    print("⚠️  OpenCLIP not available. Install with: pip install open-clip-torch")

# Configuration
MODEL_CONFIGS = {
    "ViT-B/32": {
        "model_name": "ViT-B/32",
        "output_file": "clip_vit_b32_text_encoder_full.onnx",
        "embedding_dim": 512,
        "library": "openai"  # Uses OpenAI CLIP
    },
    "RN50x4": {
        "model_name": "RN50x4", 
        "output_file": "clip_resnet50x4_text_encoder_full.onnx",
        "embedding_dim": 640,
        "library": "openai"  # Uses OpenAI CLIP
    },
    "ViT-L/14-laion2B": {
        "model_name": "ViT-L-14",
        "pretrained": "laion2b_s32b_b82k",
        "output_file": "clip_vit_l14_laion2b_text_encoder_full.onnx",
        "embedding_dim": 768,
        "library": "open_clip"  # Uses OpenCLIP, NOTE: compatible with HuggingFace when exporting embedding, projection matrix
    }
}

def export_clip_text_encoder(model_key, device="cpu"):
    """Export CLIP text encoder to ONNX format"""
    
    config = MODEL_CONFIGS[model_key]
    library = config.get("library", "openai")
    
    # Check if required library is available
    if library == "openai" and not OPENAI_CLIP_AVAILABLE:
        print(f"❌ OpenAI CLIP is required for {config['model_name']} but not installed")
        print("   Install with: pip install git+https://github.com/openai/CLIP.git")
        return False
    
    if library == "open_clip" and not OPEN_CLIP_AVAILABLE:
        print(f"❌ OpenCLIP is required for {config['model_name']} but not installed")
        print("   Install with: pip install open-clip-torch")
        return False
    
    print(f"🚀 Starting export for {config['model_name']} using {library}")
    
    try:
        # Load CLIP model based on library
        print(f"📥 Loading {config['model_name']} model...")
        
        if library == "openai":
            model, preprocess = clip.load(config["model_name"], device=device)
            tokenize_func = clip.tokenize
        else:  # open_clip
            pretrained = config.get("pretrained", "openai")
            model, _, preprocess = open_clip.create_model_and_transforms(
                config["model_name"],
                pretrained=pretrained,
                device=device
            )
            tokenize_func = open_clip.tokenize
        
        model.eval()
        print(f"✅ Model loaded successfully")
        
        # Wrap encode_text in a nn.Module
        class CLIPTextEncoder(nn.Module):
            def __init__(self, clip_model):
                super(CLIPTextEncoder, self).__init__()
                self.clip_model = clip_model

            def forward(self, input_ids):
                with torch.no_grad():
                    return self.clip_model.encode_text(input_ids)

        # Instantiate the wrapper
        text_encoder = CLIPTextEncoder(model).to(device)
        
        # Create dummy input with correct dtype (int32 for ONNX compatibility)
        print("🔧 Preparing dummy input...")
        dummy_text = ["a photo of a cat", "a beautiful sunset"]  # Multiple examples for robustness
        text_tokens = tokenize_func(dummy_text).to(device)
        
        # Convert to int32 for ONNX compatibility
        text_tokens_int32 = text_tokens.to(torch.int32)
        
        # Test the model first
        print("🧪 Testing model with dummy input...")
        with torch.no_grad():
            test_output = text_encoder(text_tokens_int32)
            print(f"✅ Test successful. Output shape: {test_output.shape}")
            print(f"   Expected embedding dim: {config['embedding_dim']}")
            
            if test_output.shape[-1] != config['embedding_dim']:
                print(f"⚠️  Warning: Output dim {test_output.shape[-1]} != expected {config['embedding_dim']}")
        
        # Export to ONNX with more robust settings
        print(f"📤 Exporting to {config['output_file']}...")
        
        torch.onnx.export(
            text_encoder,
            (text_tokens_int32,),
            config["output_file"],
            input_names=["input_ids"],
            output_names=["text_embeds"],
            dynamic_axes={
                "input_ids": {0: "batch_size"},
                "text_embeds": {0: "batch_size"}
            },
            opset_version=14,  # More stable than 14 for some models
            do_constant_folding=False,  # Disable for compatibility
            verbose=False,  # Reduce noise
            export_params=True,
            keep_initializers_as_inputs=False
        )
        
        # Verify the exported model
        print("🔍 Verifying exported model...")
        try:
            import onnx
            import onnxruntime as ort
            
            # Check ONNX model validity
            onnx_model = onnx.load(config["output_file"])
            onnx.checker.check_model(onnx_model)
            print("✅ ONNX model structure is valid")
            
            # Test with ONNX Runtime
            session = ort.InferenceSession(config["output_file"])
            
            # Get input/output info
            input_info = session.get_inputs()[0]
            output_info = session.get_outputs()[0]
            
            print(f"📋 Input info: {input_info.name}, shape: {input_info.shape}, type: {input_info.type}")
            print(f"📋 Output info: {output_info.name}, shape: {output_info.shape}, type: {output_info.type}")
            
            # Test inference
            dummy_input = text_tokens_int32.numpy()
            ort_outputs = session.run(None, {input_info.name: dummy_input})
            
            print(f"✅ ONNX Runtime test successful. Output shape: {ort_outputs[0].shape}")
            
            # Compare with PyTorch output
            pytorch_output = test_output.numpy()
            onnx_output = ort_outputs[0]
            
            max_diff = abs(pytorch_output - onnx_output).max()
            print(f"📊 Max difference between PyTorch and ONNX: {max_diff:.6f}")
            
            if max_diff < 1e-4:
                print("✅ PyTorch and ONNX outputs match closely")
            else:
                print("⚠️  Warning: Significant difference between PyTorch and ONNX outputs")
            
        except ImportError:
            print("⚠️  onnx or onnxruntime not installed. Skipping verification.")
        except Exception as e:
            print(f"❌ Verification failed: {e}")
            return False
        
        # Check file size
        file_size = os.path.getsize(config["output_file"]) / (1024 * 1024)  # MB
        print(f"📁 File size: {file_size:.2f} MB")
        
        if file_size < 1:
            print("⚠️  Warning: File size seems too small")
            return False
        
        print(f"🎉 Successfully exported {config['model_name']} to {config['output_file']}")
        return True
        
    except Exception as e:
        print(f"❌ Export failed for {config['model_name']}: {e}")
        import traceback
        traceback.print_exc()
        return False

def parse_arguments():
    """Parse command line arguments"""
    parser = argparse.ArgumentParser(
        description="Export CLIP text encoder to ONNX format",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=f"""
Available networks to export:
{chr(10).join([f"  • {key}: {config['model_name']} (embedding_dim: {config['embedding_dim']})" 
               for key, config in MODEL_CONFIGS.items()])}

Examples:
  python {os.path.basename(__file__)} ViT-B/32
  python {os.path.basename(__file__)} RN50x4
  python {os.path.basename(__file__)} --list
  python {os.path.basename(__file__)} --all
        """
    )
    
    # Mutually exclusive group for different modes
    group = parser.add_mutually_exclusive_group(required=True)
    
    group.add_argument(
        'network', 
        nargs='?',
        choices=list(MODEL_CONFIGS.keys()),
        help='Network to export (choose from available networks listed above)'
    )
    
    group.add_argument(
        '--list', '-l',
        action='store_true',
        help='List all available networks and exit'
    )
    
    group.add_argument(
        '--all', '-a',
        action='store_true', 
        help='Export all available networks'
    )
    
    parser.add_argument(
        '--device',
        default='cpu',
        choices=['cpu', 'cuda'],
        help='Device to use for export (default: cpu)'
    )
    
    return parser.parse_args()

def list_available_networks():
    """List all available networks"""
    print("🎯 Available CLIP Text Encoder Networks")
    print("=" * 50)
    
    for key, config in MODEL_CONFIGS.items():
        library = config.get("library", "openai")
        available = (library == "openai" and OPENAI_CLIP_AVAILABLE) or \
                   (library == "open_clip" and OPEN_CLIP_AVAILABLE)
        status = "✅" if available else "❌"
        
        print(f"{status} {key}")
        print(f"   Model: {config['model_name']}")
        if library == "open_clip" and "pretrained" in config:
            print(f"   Pretrained: {config['pretrained']}")
        print(f"   Output file: {config['output_file']}")
        print(f"   Embedding dimension: {config['embedding_dim']}")
        print(f"   Library: {library}")
        if not available:
            if library == "openai":
                print(f"   ⚠️  Install: pip install git+https://github.com/openai/CLIP.git")
            else:
                print(f"   ⚠️  Install: pip install open-clip-torch")
        print()

def main():
    """Main function with argument parsing"""
    args = parse_arguments()
    
    # Handle list option
    if args.list:
        list_available_networks()
        return
    
    # Determine which networks to export
    if args.all:
        networks_to_export = list(MODEL_CONFIGS.keys())
        print("🎯 CLIP Text Encoder ONNX Export Script - All Networks")
    else:
        networks_to_export = [args.network]
        print(f"🎯 CLIP Text Encoder ONNX Export Script - {args.network}")
    
    print("=" * 60)
    
    # Export selected networks
    results = {}
    
    for model_key in networks_to_export:
        print(f"\n{'='*20} {model_key} {'='*20}")
        results[model_key] = export_clip_text_encoder(model_key, args.device)
        print()
    
    # Summary
    print("📊 EXPORT SUMMARY")
    print("=" * 50)
    for model_key, success in results.items():
        status = "✅ SUCCESS" if success else "❌ FAILED"
        print(f"{model_key}: {status}")
    
    successful_exports = sum(results.values())
    total_exports = len(results)
    
    if successful_exports == total_exports:
        print(f"\n🎉 All {total_exports} models exported successfully!")
    else:
        print(f"\n⚠️  {successful_exports}/{total_exports} models exported successfully")
        sys.exit(1)  # Exit with error code if not all exports succeeded
    
    print("\n📁 Output files:")
    for model_key in networks_to_export:
        config = MODEL_CONFIGS[model_key]
        if os.path.exists(config["output_file"]):
            file_size = os.path.getsize(config["output_file"]) / (1024 * 1024)
            print(f"  ✅ {config['output_file']} ({file_size:.2f} MB)")
        else:
            print(f"  ❌ {config['output_file']} (missing)")

if __name__ == "__main__":
    main()