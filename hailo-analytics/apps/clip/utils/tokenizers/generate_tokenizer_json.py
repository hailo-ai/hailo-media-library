#!/usr/bin/env python3

import os
import sys
import subprocess
import pkg_resources
from pathlib import Path

# Required package versions for H15 compatibility
REQUIRED_VERSIONS = {
    'tokenizers': '0.13.3',
    'transformers': '4.28.1'
}

VENV_NAME = 'venv_old_tokenizer'

def check_package_version(package_name, required_version):
    """Check if a package is installed with the required version."""
    try:
        installed_version = pkg_resources.get_distribution(package_name).version
        return installed_version == required_version
    except pkg_resources.DistributionNotFound:
        return False

def create_venv_and_install():
    """Create virtual environment and install required packages."""
    print(f"Creating virtual environment: {VENV_NAME}")
    
    # Create virtual environment
    subprocess.run([sys.executable, '-m', 'venv', VENV_NAME], check=True)
    
    # Get paths for the virtual environment
    venv_python = os.path.join(VENV_NAME, 'bin', 'python')
    venv_pip = os.path.join(VENV_NAME, 'bin', 'pip')
    
    # Install required packages
    print("Installing required packages...")
    for package, version in REQUIRED_VERSIONS.items():
        package_spec = f"{package}=={version}"
        print(f"Installing {package_spec}")
        subprocess.run([venv_pip, 'install', package_spec], check=True)
    
    return venv_python

def check_venv_packages():
    """Check if virtual environment exists and has correct package versions."""
    venv_python = os.path.join(VENV_NAME, 'bin', 'python')
    
    if not os.path.exists(venv_python):
        return False, None
    
    # Check package versions in the virtual environment
    for package, required_version in REQUIRED_VERSIONS.items():
        try:
            result = subprocess.run([
                venv_python, '-c', 
                f'import pkg_resources; print(pkg_resources.get_distribution("{package}").version)'
            ], capture_output=True, text=True, check=True)
            
            installed_version = result.stdout.strip()
            if installed_version != required_version:
                print(f"Package {package} version mismatch: found {installed_version}, required {required_version}")
                return False, venv_python
        except subprocess.CalledProcessError:
            print(f"Package {package} not found in virtual environment")
            return False, venv_python
    
    return True, venv_python

def setup_environment():
    """Setup virtual environment with required package versions."""
    # Check if virtual environment exists and has correct packages
    venv_valid, venv_python = check_venv_packages()
    
    if not venv_valid:
        # Remove existing venv if it exists but is invalid
        if os.path.exists(VENV_NAME):
            print(f"Removing invalid virtual environment: {VENV_NAME}")
            subprocess.run(['rm', '-rf', VENV_NAME], check=True)
        
        # Create new virtual environment and install packages
        venv_python = create_venv_and_install()
    else:
        print(f"Virtual environment {VENV_NAME} already exists with correct package versions")
    
    return venv_python

def run_tokenizer_generation(venv_python):
    """Run the tokenizer generation using the virtual environment."""
    print("Generating tokenizer JSON file...")
    
    # Script to run in the virtual environment
    script_content = '''
from transformers import CLIPTokenizerFast
import os

# Create dist directory if it doesn't exist
os.makedirs("dist", exist_ok=True)

# Generate tokenizer
tok = CLIPTokenizerFast.from_pretrained("openai/clip-vit-base-patch32")
tok.save_pretrained("dist")

print("Tokenizer JSON file generated successfully in 'dist' folder!")
'''
    
    # Run the script in the virtual environment
    result = subprocess.run([venv_python, '-c', script_content], check=True)
    
def main():
    """Main function to setup environment and generate tokenizer."""
    print("=" * 60)
    print("CLIP Tokenizer JSON Generator")
    print("=" * 60)
    print("This script generates a tokenizer JSON file for the CLIP model.")
    print("The generated tokenizer.json will work for all CLIP text encoding variants")
    print("such as resnet50x4, vit b32, vit L/14, etc...")
    print("=" * 60)
    
    # Setup virtual environment
    venv_python = setup_environment()
    
    # Generate tokenizer
    run_tokenizer_generation(venv_python)
    
    print("=" * 60)
    print("SUCCESS: Tokenizer generation completed!")
    print(f"You can find tokenizer.json in the 'dist' folder")
    print("=" * 60)

if __name__ == "__main__":
    main()
