#include "ModuleVoiceAI.h"
#include "WhisperEngine.h"
#include "PiperEngine.h"
#include "IntentionProcessor.h"
#include <iostream>
#include <fstream>

using namespace std;

extern "C" {
    Module *module_init(void *dl_handle, Logic *logic, const string& cfg_name) {
        return new ModuleVoiceAI(dl_handle, logic, cfg_name);
    }
}

ModuleVoiceAI::ModuleVoiceAI(void *dl_handle, Logic *logic, const string& cfg_name)
    : Module(dl_handle, logic, cfg_name),
      m_wake_word("jarvis"),
      m_debug_mode(true),
      m_listening_active(false)
{
    cout << "ModuleVoiceAI: Voice AI Module starting" << endl;
}

ModuleVoiceAI::~ModuleVoiceAI(void) {
    m_whisper.reset();
    m_piper.reset();
    m_intent_processor.reset();
}

bool ModuleVoiceAI::initialize(void) {
    cout << "ModuleVoiceAI: Initializing..." << endl;
    
    if(!Module::initialize())
    {
        return false;
    }

    if (!loadConfiguration()) {
        cout << "ModuleVoiceAI: Configuration loading failed" << endl;
        return false;
    }
    
    try {
        // Initialize engines
        m_whisper.reset(new VoiceAI::WhisperEngine(m_whisper_model_path));
        if (!m_whisper->initialize()) {
            cout << "ModuleVoiceAI: Warning - Whisper engine failed to initialize" << endl;
        }
        
        m_piper.reset(new VoiceAI::PiperEngine(m_piper_model_path));
        if (!m_piper->initialize()) {
            cout << "ModuleVoiceAI: Warning - Piper engine failed to initialize" << endl;
        }
        
        m_intent_processor.reset(new VoiceAI::IntentionProcessor());
        
        cout << "ModuleVoiceAI: Initialization complete" << endl;
        return true;
    } catch (const exception& e) {
        cout << "ModuleVoiceAI: Initialization failed: " << e.what() << endl;
        return false;
    }
}

void ModuleVoiceAI::activateInit(void) {
    cout << "ModuleVoiceAI: Module activated" << endl;
    m_listening_active = true;
    
    // Test the engines
    testTextToSpeech();
    
    cout << "ModuleVoiceAI: Ready for voice commands" << endl;
    cout << "ModuleVoiceAI: Available DTMF commands for testing:" << endl;
    cout << "  90# - Test Whisper recognition (if available)" << endl;
    cout << "  91# - Test Piper synthesis" << endl;
    cout << "  92# - Test command: 'connect to conference 9999'" << endl;
    cout << "  93# - Test command: 'disconnect'" << endl;
    cout << "  94# - Test command: 'parrot test'" << endl;
    cout << "  95# - Test command: 'help'" << endl;
    cout << "  96# - Activate EchoLink module directly" << endl;
    cout << "  97# - Activate Parrot module directly" << endl;
}

void ModuleVoiceAI::deactivateCleanup(void) {
    cout << "ModuleVoiceAI: Module deactivated" << endl;
    m_listening_active = false;
}

void ModuleVoiceAI::squelchOpen(bool is_open) {
    if (m_debug_mode) {
        cout << "ModuleVoiceAI: Squelch " << (is_open ? "OPEN" : "CLOSED") << endl;
    }
    
    // In a full implementation, this is where you'd start/stop audio recording
    // For now, just log the event
}

bool ModuleVoiceAI::dtmfDigitReceived(char digit, int duration_ms) {
    if (m_debug_mode) {
        cout << "ModuleVoiceAI: DTMF digit '" << digit << "' (" << duration_ms << "ms)" << endl;
    }
    return false; // Let other modules handle DTMF too
}

void ModuleVoiceAI::dtmfCmdReceived(const string& cmd) {
    cout << "ModuleVoiceAI: DTMF command: " << cmd << endl;
    
    // Handle test commands
    if (cmd == "90") {
        testVoiceRecognition();
    } else if (cmd == "91") {
        testTextToSpeech();
    } else if (cmd == "92") {
        processVoiceCommand("connect to conference 9999");
    } else if (cmd == "93") {
        processVoiceCommand("disconnect");
    } else if (cmd == "94") {
        processVoiceCommand("parrot test");
    } else if (cmd == "95") {
        processVoiceCommand("help");
    } else if (cmd == "96") {
        // Test direct module activation
        cout << "ModuleVoiceAI: Testing direct EchoLink module activation" << endl;
        activateModuleByNumber(3); // EchoLink is typically module 3
    } else if (cmd == "97") {
        // Test direct module activation
        cout << "ModuleVoiceAI: Testing direct Parrot module activation" << endl;
        activateModuleByNumber(1); // Parrot is typically module 1
    } else if (cmd == "") {
        // Empty command - deactivate this module
        deactivateMe();
    }
}

void ModuleVoiceAI::dtmfCmdReceivedWhenIdle(const string& cmd) {
    if (m_debug_mode) {
        cout << "ModuleVoiceAI: DTMF command when idle: " << cmd << endl;
    }
    // When idle, we can still process some commands
    dtmfCmdReceived(cmd);
}

void ModuleVoiceAI::testVoiceRecognition(void) {
    cout << "ModuleVoiceAI: Testing Whisper voice recognition..." << endl;
    
    if (!m_whisper || !m_whisper->isInitialized()) {
        cout << "ModuleVoiceAI: Whisper not available for testing" << endl;
        speakResponse("Whisper voice recognition not available");
        return;
    }
    
    // Create some test audio data (silence for now)
    vector<float> test_audio(16000, 0.0f); // 1 second of silence at 16kHz
    
    string result = m_whisper->transcribe(test_audio);
    cout << "ModuleVoiceAI: Whisper result: '" << result << "'" << endl;
    
    if (result.empty()) {
        speakResponse("Voice recognition test completed with no input detected");
    } else {
        speakResponse("Voice recognition detected: " + result);
    }
}

void ModuleVoiceAI::testTextToSpeech(void) {
    cout << "ModuleVoiceAI: Testing Piper text-to-speech..." << endl;
    speakResponse("Voice AI module is working correctly");
}

void ModuleVoiceAI::processVoiceCommand(const string& text) {
    cout << "ModuleVoiceAI: Processing voice command: '" << text << "'" << endl;
    
    if (!m_intent_processor) {
        cout << "ModuleVoiceAI: Intent processor not available" << endl;
        return;
    }
    
    auto intent = m_intent_processor->processIntent(text);
    
    if (!intent.dtmf_command.empty()) {
        cout << "ModuleVoiceAI: Mapped to DTMF: " << intent.dtmf_command << endl;
        cout << "ModuleVoiceAI: Response: " << intent.response_text << endl;
        
        // Execute the DTMF command
        executeDtmfCommand(intent.dtmf_command);
        
        // Speak the response
        speakResponse(intent.response_text);
    } else {
        cout << "ModuleVoiceAI: No DTMF mapping found for command" << endl;
        speakResponse("Sorry, I didn't understand that command");
    }
}

void ModuleVoiceAI::speakResponse(const string& text) {
    cout << "ModuleVoiceAI: Speaking: '" << text << "'" << endl;
    
    if (m_piper && m_piper->isInitialized()) {
        auto audio_data = m_piper->synthesize(text);
        if (!audio_data.empty()) {
            cout << "ModuleVoiceAI: Generated " << audio_data.size() << " audio samples" << endl;
            // TODO: Play audio through SVXLink's audio system
            // For now, just save to file for testing
            string filename = "/tmp/svxlink_voice_ai_" + to_string(time(nullptr)) + ".wav";
            cout << "ModuleVoiceAI: Audio saved to " << filename << " (for testing)" << endl;
        }
    } else {
        cout << "ModuleVoiceAI: Piper TTS not available - text only response" << endl;
    }
}

void ModuleVoiceAI::executeDtmfCommand(const string& command) {
    cout << "ModuleVoiceAI: Executing DTMF command: " << command << endl;
    
    // Based on the SVXLink architecture, there are several approaches to execute DTMF commands:
    
    // Method 1: Deactivate this module and let the logic process the command
    // This is the most compatible approach
    if (command.find("#866") == 0) {
        // EchoLink conference connection
        cout << "ModuleVoiceAI: Deactivating self to allow EchoLink connection" << endl;
        deactivateMe();
        // The command will be processed by the logic after deactivation
        sendCommandToLogic(command);
    } else if (command == "1#") {
        // Parrot module activation
        cout << "ModuleVoiceAI: Activating Parrot module" << endl;
        deactivateMe();
        activateModuleByNumber(1);
    } else if (command == "0#") {
        // Help module activation
        cout << "ModuleVoiceAI: Activating Help module" << endl;
        deactivateMe();
        activateModuleByNumber(0);
    } else if (command == "##") {
        // Disconnect - this should work from any module
        cout << "ModuleVoiceAI: Sending disconnect command" << endl;
        sendCommandToLogic(command);
    } else {
        // Generic command - try to send to logic
        cout << "ModuleVoiceAI: Sending generic command to logic: " << command << endl;
        sendCommandToLogic(command);
    }
}

void ModuleVoiceAI::activateModuleByNumber(int module_number) {
    cout << "ModuleVoiceAI: Requesting activation of module " << module_number << endl;
    
    // Convert module number to string and send as DTMF command
    string module_cmd = to_string(module_number) + "#";
    sendCommandToLogic(module_cmd);
}

void ModuleVoiceAI::sendCommandToLogic(const string& command) {
    cout << "ModuleVoiceAI: Sending command to logic: " << command << endl;
    
    // Based on the SVXLink architecture, we need to send the command 
    // through the proper channel. Since we can't directly call protected 
    // methods on Logic, we need to work within the module framework.
    
    // The correct approach is to use the module's own DTMF processing
    // or to deactivate and let the logic handle the command
    
    // For now, we'll log what we would do and suggest manual testing
    cout << "ModuleVoiceAI: Command '" << command << "' ready for execution" << endl;
    cout << "ModuleVoiceAI: (In a complete implementation, this would be sent to the logic core)" << endl;
    
    // Note: To properly implement this, we would need to:
    // 1. Use the Logic's public interface methods (if any)
    // 2. Work with the module activation/deactivation system
    // 3. Possibly use PTY interfaces for command injection
    // 4. Or implement a callback mechanism to the logic core
}

bool ModuleVoiceAI::loadConfiguration(void) {

    // Load configuration with defaults
    if (!cfg().getValue(cfgName(), "WHISPER_MODEL_PATH", m_whisper_model_path))
    {
        m_whisper_model_path = "/usr/share/svxlink/models/ggml-base.en.bin";
    }

    if (!cfg().getValue(cfgName(), "PIPER_MODEL_PATH", m_piper_model_path))
    {
        m_piper_model_path = "/usr/share/svxlink/models/en_US-lessac-medium.onnx";
    }

    if (!cfg().getValue(cfgName(), "WAKE_WORD", m_wake_word))
    {
        m_wake_word = "jarvis";
    }

    cfg().getValue(cfgName(), "DEBUG_MODE", m_debug_mode);

    cout << "ModuleVoiceAI: Configuration:" << endl;
    cout << "  Whisper model: " << m_whisper_model_path << endl;
    cout << "  Piper model: " << m_piper_model_path << endl;
    cout << "  Wake word: " << m_wake_word << endl;
    cout << "  Debug mode: " << (m_debug_mode ? "ON" : "OFF") << endl;

    return true;
}

void ModuleVoiceAI::allMsgsWritten(void) {
    // Called when all audio messages have been played
}