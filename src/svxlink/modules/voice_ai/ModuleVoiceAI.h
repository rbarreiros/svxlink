#ifndef MODULE_VOICE_AI_INCLUDED
#define MODULE_VOICE_AI_INCLUDED

#include <Module.h>
#include <Logic.h>
#include <version/SVXLINK.h>
#include <memory>
#include <string>
#include <thread>
#include <atomic>

// Forward declarations
namespace VoiceAI {
    class WhisperEngine;
    class PiperEngine;
    class IntentionProcessor;
}

class ModuleVoiceAI : public Module
{
public:
    ModuleVoiceAI(void *dl_handle, Logic *logic, const std::string& cfg_name);
    ~ModuleVoiceAI(void);
    
    // Module interface
    const char *compiledForVersion(void) const override { return SVXLINK_APP_VERSION; }
    bool initialize(void) override;

protected:
    void activateInit(void) override;
    void deactivateCleanup(void) override;
    void squelchOpen(bool is_open) override;
    void allMsgsWritten(void) override;
    
    // DTMF handling - correct signatures based on Module.h
    bool dtmfDigitReceived(char digit, int duration_ms) override;
    void dtmfCmdReceived(const std::string& cmd) override;
    void dtmfCmdReceivedWhenIdle(const std::string& cmd) override;

private:
    // Configuration
    std::string m_whisper_model_path;
    std::string m_piper_model_path;
    std::string m_wake_word;
    bool m_debug_mode;
    
    // AI engines
    std::unique_ptr<VoiceAI::WhisperEngine> m_whisper;
    std::unique_ptr<VoiceAI::PiperEngine> m_piper;
    std::unique_ptr<VoiceAI::IntentionProcessor> m_intent_processor;
    
    // State
    std::atomic<bool> m_listening_active;
    
    // Methods
    void testVoiceRecognition(void);
    void testTextToSpeech(void);
    void processVoiceCommand(const std::string& text);
    void speakResponse(const std::string& text);
    void executeDtmfCommand(const std::string& command);
    bool loadConfiguration(void);
    
    // Module management helpers
    void activateModuleByNumber(int module_number);
    void sendCommandToLogic(const std::string& command);
};

extern "C" {
    Module *module_init(void *dl_handle, Logic *logic, const std::string& cfg_name);
}

#endif // MODULE_VOICE_AI_INCLUDED