# SVXLink Voice AI Module

A voice-controlled AI module for SVXLink that uses OpenAI Whisper for speech recognition and Piper for text-to-speech synthesis.

## Features

- **Wake Word Activation**: Responds to configurable wake word (default: "jarvis")
- **Offline Operation**: All processing happens locally using Whisper.cpp and Piper
- **Natural Language Commands**: Converts speech to DTMF commands automatically
- **Audio Feedback**: Provides spoken confirmations and responses
- **Extensible**: Easy to add new voice commands and intentions

## Supported Commands

| Voice Command | DTMF Equivalent | Description |
|---------------|-----------------|-------------|
| "Connect to conference 9999" | #8669999 | Connect to EchoLink conference |
| "Connect to node 123456" | 3123456# | Connect to EchoLink node |
| "Disconnect" | ## | Disconnect current connection |
| "Parrot test" | 1# | Activate parrot module |
| "Help" | 0# | Play help information |
| "Identify" | *# | Transmit station ID |
| "Status" | 9# | Get system status |

## Usage Examples

1. **Basic Connection**:
   - Say: "Jarvis, connect to conference 9999"
   - Response: "Connecting to conference 9999"
   - Action: Sends DTMF `#8669999`

2. **Echo Test**:
   - Say: "Jarvis, parrot test"  
   - Response: "Activating parrot mode for testing"
   - Action: Sends DTMF `1#`

3. **Disconnect**:
   - Say: "Jarvis, disconnect"
   - Response: "Disconnecting from current connection"
   - Action: Sends DTMF `##`

## Installation

1. Run the installation script:
   ```bash
   chmod +x install.sh
   ./install.sh
   ```

2. Add to your SVXLink configuration:
   ```ini
   [SimplexLogic]
   MODULES=VoiceAI,EchoLink,Parrot,Help
   ```

3. Restart SVXLink:
   ```bash
   sudo systemctl restart svxlink
   ```

## Configuration Options

Edit `/etc/svxlink/svxlink.d/ModuleVoiceAI.conf`:

```ini
[ModuleVoiceAI]
# Wake word (case insensitive)
WAKE_WORD=jarvis

# Model paths
WHISPER_MODEL_PATH=/usr/share/svxlink/models/ggml-base.en.bin
PIPER_MODEL_PATH=/usr/share/svxlink/models/en_US-lessac-medium.onnx

# Timing settings
LISTEN_TIMEOUT_MS=5000
MIN_SPEECH_DURATION_MS=1000

# Debug mode
DEBUG_MODE=1
```

## Adding Custom Commands

Edit `IntentionProcessor.cpp` and add new patterns:

```cpp
// Add in initializeDefaultPatterns()
addCommandMapping("weather.*report", "4#", "Getting weather report");
addCommandMapping("time.*check", "5#", "Checking current time");
```

## Troubleshooting

### Common Issues:

1. **Module fails to load**:
   - Check model file paths in configuration
   - Verify Whisper and Piper models are downloaded
   - Check SVXLink logs: `journalctl -u svxlink -f`

2. **Wake word not detected**:
   - Ensure microphone levels are adequate
   - Try speaking closer to microphone
   - Enable debug mode to see recognition output

3. **Poor speech recognition**:
   - Check audio quality and noise levels
   - Consider using larger Whisper model
   - Adjust `MIN_SPEECH_DURATION_MS` setting

4. **No audio output**:
   - Verify Piper model and configuration files
   - Check SVXLink audio configuration
   - Test Piper independently: `echo "test" | piper --model your-model.onnx --output_file test.wav`

### Debugging Commands:

```bash
# Test Whisper recognition
echo "test audio" | whisper --model /usr/share/svxlink/models/ggml-base.en.bin

# Test Piper synthesis  
echo "Hello world" | piper --model /usr/share/svxlink/models/en_US-lessac-medium.onnx --output_file test.wav

# Monitor SVXLink logs
journalctl -u svxlink -f

# Check module loading
grep "VoiceAI" /var/log/svxlink.log
```

## Performance Notes

- **Raspberry Pi 4**: Works well with base model, ~2-3 second response time
- **x86 Systems**: Sub-second response time with base model
- **Memory Usage**: ~500MB for models and processing
- **CPU Usage**: Spikes during speech processing, idle otherwise

## License

This module is released under the same license as SVXLink (GPL v2+).

## Contributing

1. Fork the repository
2. Create feature branch
3. Add tests for new functionality
4. Submit pull request

For bug reports and feature requests, please use the GitHub issue tracker.
