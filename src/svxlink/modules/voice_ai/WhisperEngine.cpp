//
// WhisperEngine.cpp - Implementation
//
#include "WhisperEngine.h"
#include "whisper.h"
#include <iostream>
#include <algorithm>
#include <cmath>

using namespace std;
using namespace VoiceAI;

WhisperEngine::WhisperEngine(const string& model_path)
    : m_model_path(model_path), m_ctx(nullptr), m_initialized(false) {
}

WhisperEngine::~WhisperEngine() {
    if (m_ctx) {
        whisper_free(m_ctx);
    }
}

bool WhisperEngine::initialize() {
    if (m_initialized) return true;
    
    cout << "WhisperEngine: Loading model from " << m_model_path << endl;
    
    m_ctx = whisper_init_from_file(m_model_path.c_str());
    if (!m_ctx) {
        cout << "WhisperEngine: Failed to load model" << endl;
        return false;
    }
    
    m_initialized = true;
    cout << "WhisperEngine: Model loaded successfully" << endl;
    return true;
}

string WhisperEngine::transcribe(const vector<float>& audio_data) {
    if (!m_initialized || !m_ctx || audio_data.empty()) {
        return "";
    }
    
    // Preprocess audio (resample to 16kHz if needed, normalize, etc.)
    auto processed_audio = preprocessAudio(audio_data);
    
    // Set up whisper parameters
    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_realtime = false;
    wparams.print_progress = false;
    wparams.print_timestamps = false;
    wparams.print_special = false;
    wparams.translate = false;
    wparams.language = "en";
    wparams.n_threads = 1;
    wparams.offset_ms = 0;
    wparams.duration_ms = 0;
    
    // Run inference
    if (whisper_full(m_ctx, wparams, processed_audio.data(), processed_audio.size()) != 0) {
        cout << "WhisperEngine: Failed to process audio" << endl;
        return "";
    }
    
    // Extract text
    string result;
    const int n_segments = whisper_full_n_segments(m_ctx);
    
    for (int i = 0; i < n_segments; ++i) {
        const char* text = whisper_full_get_segment_text(m_ctx, i);
        if (text) {
            result += text;
        }
    }
    
    // Clean up the result
    result.erase(0, result.find_first_not_of(" \t\n\r"));
    result.erase(result.find_last_not_of(" \t\n\r") + 1);
    
    return result;
}

vector<float> WhisperEngine::preprocessAudio(const vector<float>& input) {
    // Whisper expects 16kHz mono audio
    vector<float> output = input;
    
    // Apply basic normalization
    if (!output.empty()) {
        float max_val = *max_element(output.begin(), output.end(),
            [](float a, float b) { return abs(a) < abs(b); });
        
        if (max_val > 0.01f) { // Avoid division by zero
            float scale = 0.9f / max_val;
            for (float& sample : output) {
                sample *= scale;
            }
        }
    }
    
    return output;
}
