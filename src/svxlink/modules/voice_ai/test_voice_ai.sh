#!/bin/bash
# test_voice_ai.sh - Test the ModuleVoiceAI functionality

echo "Testing ModuleVoiceAI module..."

# Check if SVXLink is running
if ! pgrep svxlink > /dev/null; then
    echo "Error: SVXLink is not running"
    echo "Start SVXLink first: sudo systemctl start svxlink"
    exit 1
fi

echo "SVXLink is running. Testing ModuleVoiceAI..."

# Test DTMF commands (assuming module is configured and activated)
echo "Testing Piper TTS synthesis..."
echo "91#" | nc -u localhost 5300  # Assuming DTMF PTY is available

sleep 2

echo "Testing voice command processing..."
echo "92#" | nc -u localhost 5300  # Test "connect to conference 9999"

sleep 2

echo "Testing disconnect command..."
echo "93#" | nc -u localhost 5300  # Test "disconnect"

sleep 2

echo "Testing parrot command..."
echo "94#" | nc -u localhost 5300  # Test "parrot test"

echo "Test complete. Check SVXLink logs for results:"
echo "journalctl -u svxlink -f"
