//
// Installation script: install.sh
//
#!/bin/bash

# SVXLink Voice AI Module Installation Script

set -e

echo "Installing SVXLink Voice AI Module..."

# Check if running as root
if [[ $EUID -eq 0 ]]; then
   echo "Please do not run this script as root"
   exit 1
fi

# Create directories
sudo mkdir -p /usr/share/svxlink/models
sudo mkdir -p /etc/svxlink/svxlink.d

# Install dependencies
echo "Installing dependencies..."
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libsvxlink-dev \
    python3-pip \
    wget \
    curl

# Install Piper
echo "Installing Piper TTS..."
pip3 install --user piper-tts

# Download Whisper model
echo "Downloading Whisper base model..."
WHISPER_MODEL_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin"
sudo wget -O /usr/share/svxlink/models/ggml-base.en.bin "$WHISPER_MODEL_URL"

# Download Piper model
echo "Downloading Piper voice model..."
PIPER_MODEL_URL="https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/lessac/medium/en_US-lessac-medium.onnx"
PIPER_CONFIG_URL="https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/lessac/medium/en_US-lessac-medium.onnx.json"

sudo wget -O /usr/share/svxlink/models/en_US-lessac-medium.onnx "$PIPER_MODEL_URL"
sudo wget -O /usr/share/svxlink/models/en_US-lessac-medium.onnx.json "$PIPER_CONFIG_URL"

# Build and install Whisper.cpp
echo "Building Whisper.cpp..."
if [ ! -d "whisper.cpp" ]; then
    git clone https://github.com/ggerganov/whisper.cpp.git
fi

cd whisper.cpp
make -j$(nproc)
sudo make install
cd ..

# Build the module
echo "Building Voice AI module..."
mkdir -p build
cd build
cmake ..
make -j$(nproc)
sudo make install
cd ..

# Install configuration
echo "Installing configuration..."
sudo cp ModuleVoiceAI.conf /etc/svxlink/svxlink.d/

echo "Installation complete!"
echo ""
echo "To enable the Voice AI module, add the following to your svxlink.conf:"
echo ""
echo "[SimplexLogic] or [RepeaterLogic]"
echo "MODULES=ModuleVoiceAI,...other modules..."
echo ""
echo "Then restart SVXLink:"
echo "sudo systemctl restart svxlink"
