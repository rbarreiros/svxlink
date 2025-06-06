## SVXLink ChatGPT Module Installation

### 1. File Placement

Place the files in your SVXLink source directory:

```
svxlink/
├── src/svxlink/modules/
│   ├── chatgpt/
│   │   ├── ModuleChatGPT.h
│   │   ├── ModuleChatGPT.cpp
│   │   └── ModuleChatGPT.conf
│   └── CMakeLists.txt (modify this file)
```

### 2. Prerequisites

Install required dependencies:

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install libcurl4-openssl-dev libjsoncpp-dev espeak ffmpeg sox
```

**CentOS/RHEL/Fedora:**
```bash
sudo dnf install libcurl-devel jsoncpp-devel espeak ffmpeg sox
```

### 3. Modify CMakeLists.txt

Add the ChatGPT module section to `src/svxlink/modules/CMakeLists.txt` 
(see the CMakeLists.txt section above)

### 4. Build SVXLink with ChatGPT Module

```bash
# In your SVXLink source directory
mkdir -p build
cd build

cmake -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DSYSCONF_INSTALL_DIR=/etc \
      -DLOCAL_STATE_DIR=/var \
      ..

make -j$(nproc)
sudo make install
sudo ldconfig
```

### 5. Generate Sound Files

```bash
# Make the script executable and run it
chmod +x generate_sounds.sh
sudo ./generate_sounds.sh
```

### 6. Configure API Key

1. Get an OpenAI API key from https://platform.openai.com/api-keys
2. Edit `/etc/svxlink/svxlink.d/ModuleChatGPT.conf`
3. Replace `your_openai_api_key_here` with your actual API key

### 7. Enable Module

Edit `/etc/svxlink/svxlink.conf` and add ModuleChatGPT to your logic modules:

```ini
[SimplexLogic]
MODULES=ModuleHelp,ModuleParrot,ModuleChatGPT
```

### 8. Restart SVXLink

```bash
sudo systemctl restart svxlink
```

## Usage

### DTMF Commands
- **8#** - Activate ChatGPT module
- **0** - Help
- **1** - Ready to record (then use PTT to record your question)
- **#** - Exit module

### Example Session
1. Key up and send: **8#**
2. Listen: "ChatGPT module activated"  
3. Send: **1**
4. Listen: "Ready to record. Press PTT and speak your question"
5. Press PTT and ask: "What is the weather like today?"
6. Release PTT
7. Listen: "Recording stopped, processing your request, please wait"
8. Listen to ChatGPT's response
9. Listen: "Ready for next request"
10. Repeat from step 3 or send **#** to exit

## Configuration Options

Edit `/etc/svxlink/svxlink.d/ModuleChatGPT.conf`:

- `API_KEY` - Your OpenAI API key (required)
- `CHAT_MODEL` - GPT model to use (default: gpt-3.5-turbo)
- `MAX_TOKENS` - Maximum response length (default: 150)
- `TEMPERATURE` - Response creativity 0.0-2.0 (default: 0.7)
- `TIMEOUT` - Module timeout in seconds (default: 60)

## Troubleshooting

### Module doesn't load
- Check dependencies: `ldd /usr/local/lib/svxlink/ModuleChatGPT.so`
- Check SVXLink logs: `journalctl -u svxlink -f`

### API errors
- Verify API key is correct
- Check internet connectivity
- Monitor API usage at OpenAI dashboard

### Audio issues
- Test espeak: `espeak "test"`
- Check sound file permissions in `/usr/share/svxlink/sounds/ChatGPT/`
- Verify audio levels with other modules first

### Build issues
- Ensure all dependencies are installed
- Check CMake output for missing libraries
- Verify file placement in correct directories

## Free Alternatives

To use free ChatGPT-like services:

1. **Ollama (Local)**:
   ```ini
   # In ModuleChatGPT.cpp, change API URLs to:
   # http://localhost:11434/v1/chat/completions
   ```

2. **OpenRouter (Free Tier)**:
   ```ini
   API_KEY=your_openrouter_key
   # Modify API URLs in source code to use openrouter.ai
   ```

The module is designed to be easily modified for different API endpoints.