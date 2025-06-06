#!/bin/bash

# Script to generate sound files for ChatGPT module
# Run with: sudo ./generate_sounds.sh

SOUNDS_DIR="/usr/local/share/svxlink/sounds/ChatGPT"

echo "Creating ChatGPT module sound files..."

# Create directory
mkdir -p "$SOUNDS_DIR"
cd "$SOUNDS_DIR"

# Function to create sound file
create_sound() {
    local filename="$1"
    local text="$2"
    
    echo "Creating $filename.raw..."
    espeak -w "${filename}.wav" "$text" 2>/dev/null
    
    if [ -f "${filename}.wav" ]; then
        sox "${filename}.wav" -r 16000 -c 1 -t raw "${filename}.raw" 2>/dev/null
        rm "${filename}.wav"
        echo "  ✓ Created ${filename}.raw"
    else
        echo "  ✗ Failed to create ${filename}.raw"
    fi
}

# Create all sound files
create_sound "module_activated" "ChatGPT module activated"
create_sound "help" "ChatGPT module. Press 1 to start voice request, hash to exit"
create_sound "ready_to_record" "Ready to record. Press PTT and speak your question"
create_sound "recording_started" "Recording started"
create_sound "recording_stopped" "Recording stopped"
create_sound "processing_request" "Processing your request, please wait"
create_sound "ready_for_next" "Ready for next request"
create_sound "error_occurred" "An error occurred, please try again"
create_sound "operation_timeout" "Operation timed out"

echo "Sound file generation complete!"
echo "Files created in: $SOUNDS_DIR"

# Set appropriate permissions
chown -R svxlink:svxlink "$SOUNDS_DIR" 2>/dev/null || true
chmod -R 644 "$SOUNDS_DIR"/*.raw 2>/dev/null || true

echo "Permissions set for svxlink user"
