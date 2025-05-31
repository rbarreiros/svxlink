//
// IntentionProcessor.h - Command intention processing
//
#ifndef INTENTION_PROCESSOR_H
#define INTENTION_PROCESSOR_H

#include <string>
#include <vector>
#include <map>

namespace VoiceAI {

struct Intent {
    std::string command;
    std::string dtmf_command;
    std::string response_text;
    std::map<std::string, std::string> parameters;
};

class IntentionProcessor {
public:
    IntentionProcessor();
    
    Intent processIntent(const std::string& text);
    void addCommandMapping(const std::string& pattern, const std::string& dtmf, const std::string& response);
    
private:
    struct CommandPattern {
        std::string pattern;
        std::string dtmf_command;
        std::string response_template;
        std::vector<std::string> keywords;
    };
    
    std::vector<CommandPattern> m_patterns;
    
    void initializeDefaultPatterns();
    bool matchesPattern(const std::string& text, const CommandPattern& pattern);
    std::string extractParameter(const std::string& text, const std::string& keyword);
    std::string processTemplate(const std::string& template_str, const std::map<std::string, std::string>& params);
};

} // namespace VoiceAI

#endif // INTENTION_PROCESSOR_H
