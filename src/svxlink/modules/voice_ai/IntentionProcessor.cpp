#include "IntentionProcessor.h"
#include <algorithm>
#include <regex>
#include <iostream>
#include <sstream>

using namespace std;
using namespace VoiceAI;

IntentionProcessor::IntentionProcessor() {
    initializeDefaultPatterns();
}

void IntentionProcessor::initializeDefaultPatterns() {
    // Clear any existing patterns
    m_patterns.clear();
    
    // EchoLink connections - updated for correct SVXLink syntax
    addCommandMapping("connect.*conference.*([0-9]+)", "#866{1}", "Connecting to EchoLink conference {1}");
    addCommandMapping("connect.*node.*([0-9]+)", "3{1}#", "Connecting to EchoLink node {1}");
    addCommandMapping("connect.*to.*([0-9]+)", "3{1}#", "Connecting to station {1}");
    
    // Module activations
    addCommandMapping("parrot.*test", "1#", "Activating parrot mode for testing");
    addCommandMapping("echo.*test", "1#", "Activating echo test");
    addCommandMapping("help", "0#", "Activating help system");
    
    // System commands
    addCommandMapping("disconnect", "##", "Disconnecting from current connection");
    addCommandMapping("hang.*up", "##", "Hanging up current connection");
    addCommandMapping("identify", "*#", "Transmitting station identification");
    addCommandMapping("status", "9#", "Getting system status");
    addCommandMapping("weather", "4#", "Getting weather information");
    
    // Module deactivation
    addCommandMapping("stop", "#", "Stopping current module");
    addCommandMapping("exit", "#", "Exiting current module");
    
    cout << "IntentionProcessor: Loaded " << m_patterns.size() << " command patterns" << endl;
}

Intent IntentionProcessor::processIntent(const string& text) {
    Intent intent;
    intent.command = text;
    
    string lower_text = text;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), 
                   [](unsigned char c){ return std::tolower(c); });
    
    for (const auto& pattern : m_patterns) {
        if (matchesPattern(lower_text, pattern)) {
            intent.dtmf_command = pattern.dtmf_command;
            intent.response_text = pattern.response_template;
            
            // Extract parameters using regex
            try {
                regex re(pattern.pattern);
                smatch matches;
                
                if (regex_search(lower_text, matches, re)) {
                    for (size_t i = 1; i < matches.size(); ++i) {
                        stringstream ss;
                        ss << "{" << i << "}";
                        string param_key = ss.str();
                        string param_value = matches[i].str();
                        intent.parameters[param_key] = param_value;
                        
                        // Replace in DTMF command
                        size_t pos = intent.dtmf_command.find(param_key);
                        if (pos != string::npos) {
                            intent.dtmf_command.replace(pos, param_key.length(), param_value);
                        }
                        
                        // Replace in response text
                        pos = intent.response_text.find(param_key);
                        if (pos != string::npos) {
                            intent.response_text.replace(pos, param_key.length(), param_value);
                        }
                    }
                }
            } catch (const regex_error& e) {
                cout << "IntentionProcessor: Regex error: " << e.what() << endl;
                continue;
            }
            
            break;
        }
    }
    
    return intent;
}

void IntentionProcessor::addCommandMapping(const string& pattern, const string& dtmf, const string& response) {
    CommandPattern cmd_pattern;
    cmd_pattern.pattern = pattern;
    cmd_pattern.dtmf_command = dtmf;
    cmd_pattern.response_template = response;
    
    m_patterns.push_back(cmd_pattern);
}

bool IntentionProcessor::matchesPattern(const string& text, const CommandPattern& pattern) {
    try {
        regex re(pattern.pattern);
        return regex_search(text, re);
    } catch (const regex_error& e) {
        cout << "IntentionProcessor: Regex error in pattern matching: " << e.what() << endl;
        return false;
    }
}