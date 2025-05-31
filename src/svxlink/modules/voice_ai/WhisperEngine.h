//
// WhisperEngine.h - Whisper speech recognition wrapper
//
#ifndef WHISPER_ENGINE_H
#define WHISPER_ENGINE_H

#include <string>
#include <vector>
#include <memory>

struct whisper_context;

namespace VoiceAI {

class WhisperEngine {
public:
    WhisperEngine(const std::string& model_path);
    ~WhisperEngine();
    
    bool initialize();
    std::string transcribe(const std::vector<float>& audio_data);
    bool isInitialized() const { return m_initialized; }
    
private:
    std::string m_model_path;
    whisper_context* m_ctx;
    bool m_initialized;
    
    std::vector<float> preprocessAudio(const std::vector<float>& input);
};

} // namespace VoiceAI

#endif // WHISPER_ENGINE_H
