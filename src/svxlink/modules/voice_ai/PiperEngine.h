#ifndef PIPER_ENGINE_H
#define PIPER_ENGINE_H

#include <string>
#include <vector>
#include <memory>

namespace VoiceAI {

class PiperEngine {
public:
    PiperEngine(const std::string& model_path);
    ~PiperEngine();
    
    bool initialize();
    std::vector<float> synthesize(const std::string& text);
    bool isInitialized() const { return m_initialized; }
    
private:
    std::string m_model_path;
    bool m_initialized;
    
    std::vector<float> executeCmd(const std::string& text);
};

} // namespace VoiceAI

#endif // PIPER_ENGINE_H