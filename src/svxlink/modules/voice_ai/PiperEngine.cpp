#include "PiperEngine.h"
#include <iostream>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <sys/stat.h>  // For stat()
#include <unistd.h>    // For getpid(), unlink()
#include <ctime>       // For time()

using namespace std;
using namespace VoiceAI;

PiperEngine::PiperEngine(const string& model_path)
    : m_model_path(model_path), m_initialized(false) {
}

PiperEngine::~PiperEngine() {
}

bool PiperEngine::initialize() {
    if (m_initialized) return true;
    
    // Check if model file exists (C++11 compatible way)
    struct stat buffer;
    if (stat(m_model_path.c_str(), &buffer) != 0) {
        cout << "PiperEngine: Model file not found: " << m_model_path << endl;
        return false;
    }
    
    // Check if piper binary is available
    if (system("which piper >/dev/null 2>&1") != 0) {
        cout << "PiperEngine: Piper binary not found in PATH" << endl;
        cout << "PiperEngine: Install with: pip install piper-tts" << endl;
        return false;
    }
    
    m_initialized = true;
    cout << "PiperEngine: Initialized successfully with model " << m_model_path << endl;
    return true;
}

vector<float> PiperEngine::synthesize(const string& text) {
    if (!m_initialized || text.empty()) {
        return vector<float>();
    }
    
    return executeCmd(text);
}

vector<float> PiperEngine::executeCmd(const string& text) {
    vector<float> audio_data;
    
    try {
        // Create unique temporary files using PID and timestamp
        pid_t pid = getpid();
        time_t timestamp = time(NULL);
        
        stringstream temp_wav_ss, temp_txt_ss;
        temp_wav_ss << "/tmp/svxlink_tts_" << pid << "_" << timestamp << ".wav";
        temp_txt_ss << "/tmp/svxlink_txt_" << pid << "_" << timestamp << ".txt";
        
        string temp_wav = temp_wav_ss.str();
        string temp_txt = temp_txt_ss.str();
        
        // Write text to temporary file
        ofstream txt_file(temp_txt.c_str());
        if (!txt_file.is_open()) {
            cout << "PiperEngine: Failed to create temp text file: " << temp_txt << endl;
            return audio_data;
        }
        txt_file << text;
        txt_file.close();
        
        // Build piper command
        stringstream cmd;
        cmd << "cat \"" << temp_txt << "\" | piper --model \"" << m_model_path 
            << "\" --output_file \"" << temp_wav << "\" 2>/dev/null";
        
        cout << "PiperEngine: Executing: " << cmd.str() << endl;
        
        // Execute piper
        int result = system(cmd.str().c_str());
        
        // Check if WAV file was created (C++11 compatible way)
        struct stat wav_stat;
        if (result == 0 && stat(temp_wav.c_str(), &wav_stat) == 0) {
            cout << "PiperEngine: WAV file created successfully: " << temp_wav << endl;
            
            // Read WAV file (simplified - assumes 16-bit PCM)
            ifstream wav_file(temp_wav.c_str(), ios::binary);
            if (wav_file.is_open()) {
                // Get file size
                wav_file.seekg(0, ios::end);
                size_t file_size = static_cast<size_t>(wav_file.tellg());
                wav_file.seekg(0, ios::beg);
                
                if (file_size > 44) {  // Minimum WAV file size
                    // Skip WAV header (44 bytes)
                    wav_file.seekg(44);
                    
                    // Calculate audio data size
                    size_t audio_bytes = file_size - 44;
                    size_t audio_samples = audio_bytes / 2; // 16-bit samples
                    
                    // Read raw audio data
                    vector<int16_t> raw_audio(audio_samples);
                    wav_file.read(reinterpret_cast<char*>(&raw_audio[0]), audio_bytes);
                    
                    // Convert to float (-1.0 to 1.0 range)
                    audio_data.reserve(audio_samples);
                    for (size_t i = 0; i < raw_audio.size(); ++i) {
                        audio_data.push_back(static_cast<float>(raw_audio[i]) / 32768.0f);
                    }
                    
                    cout << "PiperEngine: Successfully read " << audio_data.size() 
                         << " audio samples from WAV file" << endl;
                } else {
                    cout << "PiperEngine: WAV file too small: " << file_size << " bytes" << endl;
                }
                
                wav_file.close();
            } else {
                cout << "PiperEngine: Failed to open WAV file for reading: " << temp_wav << endl;
            }
        } else {
            cout << "PiperEngine: Piper command failed or no output file created" << endl;
            cout << "PiperEngine: Command result: " << result << endl;
        }
        
        // Clean up temporary files (C++11 compatible way)
        if (unlink(temp_wav.c_str()) != 0) {
            // File might not exist, that's okay
        }
        if (unlink(temp_txt.c_str()) != 0) {
            // File might not exist, that's okay
        }
        
    } catch (const exception& e) {
        cout << "PiperEngine: Error synthesizing speech: " << e.what() << endl;
    }
    
    return audio_data;
}

//
// Test program for PiperEngine
//
#ifdef PIPER_ENGINE_TEST

#include <iostream>
#include <fstream>

int main() {
    using namespace VoiceAI;
    
    cout << "Testing PiperEngine..." << endl;
    
    // Test with a model path (adjust as needed)
    string model_path = "/usr/share/svxlink/models/en_US-lessac-medium.onnx";
    
    PiperEngine piper(model_path);
    
    if (!piper.initialize()) {
        cout << "Failed to initialize PiperEngine" << endl;
        return 1;
    }
    
    cout << "PiperEngine initialized successfully" << endl;
    
    // Test synthesis
    string test_text = "Hello, this is a test of the Piper text to speech engine.";
    vector<float> audio = piper.synthesize(test_text);
    
    if (audio.empty()) {
        cout << "Failed to synthesize audio" << endl;
        return 1;
    }
    
    cout << "Successfully synthesized " << audio.size() << " audio samples" << endl;
    
    // Save as raw audio for testing
    ofstream raw_file("test_output.raw", ios::binary);
    raw_file.write(reinterpret_cast<const char*>(&audio[0]), audio.size() * sizeof(float));
    raw_file.close();
    
    cout << "Audio saved to test_output.raw" << endl;
    cout << "Play with: sox -r 22050 -b 32 -e float -c 1 test_output.raw test_output.wav" << endl;
    
    return 0;
}

#endif // PIPER_ENGINE_TEST