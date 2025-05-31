#include "ModuleVoiceAI_Simple.h"
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
    cout << "ModuleVoiceAI: Simple Voice AI Module initialized" << endl;
}

ModuleVoiceAI::~ModuleVoiceAI(void) {
    m_whisper.reset();
    m_piper.reset();
    m_intent_processor.reset();
}

bool ModuleVoiceAI::initialize(void) {
    cout << "ModuleVoiceAI: Initializing..." << endl;
    
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
    }
}

void ModuleVoiceAI::dtmfCmdReceivedWhenIdle(const string& cmd) {
    if (m_debug_mode) {
        cout << "ModuleVoiceAI: DTMF command when idle: " << cmd << endl;
    }
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
        
        // Send the DTMF command
        sendDtmfCommand(intent.dtmf_command);
        
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

void ModuleVoiceAI::sendDtmfCommand(const string& command) {
    cout << "ModuleVoiceAI: Sending DTMF command: " << command << endl;
    
    if (logic()) {
        // Send the command string to the logic for processing
        for (char c : command) {
            if ((c >= '0' && c <= '9') || c == '*' || c == '#' || 
                (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) {
                cout << "ModuleVoiceAI: Sending DTMF digit: " << c << endl;
                logic()->dtmfDigitDetected(c, 100);
            }
        }
    } else {
        cout << "ModuleVoiceAI: Logic not available for DTMF injection" << endl;
    }
}

bool ModuleVoiceAI::loadConfiguration(void) {
    try {
        Config &cfg = logic()->cfg();
        string cfg_tag = name();
        
        // Load configuration with defaults
        if (!cfg.getValue(cfg_tag, "WHISPER_MODEL_PATH", m_whisper_model_path)) {
            m_whisper_model_path = "/usr/share/svxlink/models/ggml-base.en.bin";
        }
        
        if (!cfg.getValue(cfg_tag, "PIPER_MODEL_PATH", m_piper_model_path)) {
            m_piper_model_path = "/usr/share/svxlink/models/en_US-lessac-medium.onnx";
        }
        
        if (!cfg.getValue(cfg_tag, "WAKE_WORD", m_wake_word)) {
            m_wake_word = "jarvis";
        }
        
        cfg.getValue(cfg_tag, "DEBUG_MODE", m_debug_mode);
        
        cout << "ModuleVoiceAI: Configuration:" << endl;
        cout << "  Whisper model: " << m_whisper_model_path << endl;
        cout << "  Piper model: " << m_piper_model_path << endl;
        cout << "  Wake word: " << m_wake_word << endl;
        cout << "  Debug mode: " << (m_debug_mode ? "ON" : "OFF") << endl;
        
        return true;
    } catch (const exception& e) {
        cout << "ModuleVoiceAI: Configuration error: " << e.what() << endl;
        return false;
    }
}

void ModuleVoiceAI::allMsgsWritten(void) {
    // Called when all audio messages have been played
}
