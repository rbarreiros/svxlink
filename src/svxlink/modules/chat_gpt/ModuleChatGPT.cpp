
#include "ModuleChatGPT.h"

#include <AsyncConfig.h>

#include <iostream>
#include <fstream>
#include <string>

#ifdef HAVE_JSONCPP
#include <json/json.h>
#endif

#ifdef HAVE_CURL
#include <curl/curl.h>
#endif

using namespace std;
using namespace Async;

extern "C"
{
    Module* module_init(void* dl_handle, Logic* logic, const char* cfg_name)
    {
        return new ModuleChatGPT(dl_handle, logic, cfg_name);
    }
} /* extern "C" */

ModuleChatGPT::ModuleChatGPT(void* dl_handle, Logic* logic,
                             const string& cfg_name)
    : Module(dl_handle, logic, cfg_name),
      m_state(STATE_IDLE),
      m_keyword("chatgpt"),
      m_chat_model("gpt-3.5-turbo"),
      m_timeout(30000),
      m_max_tokens(150),
      m_temperature(0.7),
      m_recording(false),
      m_temp_audio_file("/tmp/svxlink_chatgpt/recording.wav"),
      m_recorded_samples_file("/tmp/svxlink_chatgpt/samples.wav"),
      m_timeout_timer(nullptr),
      m_samples_available(false)
{
    cout << "\tModule ChatGPT v1 starting...\n";
}

ModuleChatGPT::~ModuleChatGPT(void) { cleanup(); }

void ModuleChatGPT::resumeOutput(void) 
{}

void ModuleChatGPT::allSamplesFlushed(void) 
{
  cout << "Response playback finished\n";
  
  m_state = STATE_IDLE;
  
  stringstream ss;
  ss << "ready_for_next";
  processEvent(ss.str());
}

int ModuleChatGPT::writeSamples(const float* samples, int count)
{
  // If we're recording, capture the samples
  if (m_recording && samples && count > 0)
  {
    for (int i = 0; i < count; i++)
    {
      m_recorded_samples.push_back(samples[i]);
    }
  }
  
  // Always pass samples through (don't consume them)
  return count;
}

void ModuleChatGPT::flushSamples(void) 
{}

bool ModuleChatGPT::initialize(void)
{
    if (!Module::initialize())
    {
        return false;
    }

    string value;

    // Read configuration values
    if (cfg().getValue(cfgName(), "KEYWORD", value))
    {
        m_keyword = value;
    }

    if (!cfg().getValue(cfgName(), "API_KEY", m_api_key))
    {
        cerr << "*** WARNING: Config variable " << cfgName()
             << "/API_KEY not set\n";
        cerr << "*** Module will work in test mode without API functionality\n";
        m_api_key = "test_key";
    }

    if (cfg().getValue(cfgName(), "CHAT_MODEL", value))
    {
        m_chat_model = value;
    }

    if (cfg().getValue(cfgName(), "TIMEOUT", value))
    {
        m_timeout = atoi(value.c_str()) * 1000;
    }

    if (cfg().getValue(cfgName(), "MAX_TOKENS", value))
    {
        m_max_tokens = atoi(value.c_str());
    }

    if (cfg().getValue(cfgName(), "TEMPERATURE", value))
    {
        m_temperature = atof(value.c_str());
    }

    // Create temporary directory
    if (system("mkdir -p /tmp/svxlink_chatgpt") != 0)
    {
        cerr << "*** WARNING: Could not create temp directory\n";
    }

    // Initialize timeout timer - be very careful here
    if (m_timeout_timer != nullptr)
    {
        delete m_timeout_timer;
        m_timeout_timer = nullptr;
    }

    m_timeout_timer = new Timer(m_timeout);
    if (m_timeout_timer == nullptr)
    {
        cerr << "*** ERROR: Could not create timeout timer\n";
        return false;
    }

    // Connect signal carefully
    m_timeout_timer->expired.connect(
        sigc::mem_fun(*this, &ModuleChatGPT::onTimeout));

    cout << "\tModule " << name() << " initialized successfully\n";

    return true;
}

void ModuleChatGPT::activateInit(void) 
{
  cout << "*** Module " << name() << " activated\n";
  m_state = STATE_IDLE;
  
  stringstream ss;
  ss << "module_activated";
  processEvent(ss.str());
}

void ModuleChatGPT::deactivateCleanup(void) 
{
  cout << "*** Module " << name() << " deactivated\n";
  cleanup();
}

bool ModuleChatGPT::dtmfDigitReceived(char digit, int duration) 
{ 
  cout << "DTMF digit received: " << digit << endl;
  return false;
}

void ModuleChatGPT::dtmfCmdReceived(const std::string& cmd) 
{
  cout << "DTMF command: " << cmd << endl;
  
  if (cmd == "0")
  {
    stringstream ss;
    ss << "help";
    processEvent(ss.str());
  }
  else if (cmd == "1")
  {
    if (m_state == STATE_IDLE)
    {
      cout << "Starting ChatGPT session\n";
      m_state = STATE_LISTENING;
      
      stringstream ss;
      ss << "ready_to_record";
      processEvent(ss.str());
    }
  }
  else if (cmd == "#")
  {
    cleanup();
    deactivate();
  }
}

void ModuleChatGPT::dtmfCmdReceivedWhenIdle(const std::string& cmd) 
{
  dtmfCmdReceived(cmd);
}

void ModuleChatGPT::squelchOpen(bool is_open) 
{
  if (m_state == STATE_LISTENING)
  {
    if (is_open && !m_recording)
    {
      cout << "Squelch opened, starting recording\n";
      startRecording();
    }
    else if (!is_open && m_recording)
    {
      cout << "Squelch closed, stopping recording and processing\n";
      stopRecording();
      processAudioRequest();
    }
  }
}

void ModuleChatGPT::allMsgsWritten(void) 
{
  allSamplesFlushed();
}

// Implementation

void ModuleChatGPT::startRecording(void) 
{
  if (m_recording) return;
  
  cout << "Starting audio recording...\n";
  
  // Clear previous recording
  m_recorded_samples.clear();
  m_samples_available = false;
  
  m_recording = true;
  m_timeout_timer->setEnable(true);
  
  stringstream ss;
  ss << "recording_started";
  processEvent(ss.str());
}

void ModuleChatGPT::stopRecording(void) 
{
  if (!m_recording) return;
  
  cout << "Stopping audio recording...\n";
  
  m_recording = false;
  m_timeout_timer->setEnable(false);
  
  // Save recorded samples to file if we have any
  if (!m_recorded_samples.empty())
  {
    saveSamplesToFile(m_recorded_samples, m_recorded_samples_file);
    m_samples_available = true;
  }
  
  stringstream ss;
  ss << "recording_stopped";
  processEvent(ss.str());
}

void ModuleChatGPT::processAudioRequest(void) 
{
  if (m_state != STATE_LISTENING) return;
  
  cout << "Processing audio request...\n";
  m_state = STATE_PROCESSING;
  
  stringstream ss;
  ss << "processing_request";
  processEvent(ss.str());
  
  // Check if audio file exists and has reasonable content
  if (!m_samples_available || m_recorded_samples.empty())
  {
    cout << "No audio samples recorded\n";
    stringstream ss;
    ss << "error_occurred";
    processEvent(ss.str());
    m_state = STATE_IDLE;
    return;
  }
  
  sendToAPI(m_recorded_samples_file);
}

void ModuleChatGPT::sendToAPI(const std::string& audio_file) 
{
#ifdef HAVE_CURL
  cout << "Sending audio to OpenAI API...\n";
  
  string transcription;
  if (transcribeAudio(audio_file, transcription))
  {
    cout << "Transcription: " << transcription << endl;
    
    string chat_response;
    if (getChatResponse(transcription, chat_response))
    {
      cout << "ChatGPT response: " << chat_response << endl;
      string response_file = "/tmp/svxlink_chatgpt/response.wav";
      textToSpeech(chat_response, response_file);
      playFile(response_file);
      m_state = STATE_SPEAKING;
    }
    else
    {
      stringstream ss;
      ss << "error_occurred";
      processEvent(ss.str());
      m_state = STATE_IDLE;
    }
  }
  else
  {
    stringstream ss;
    ss << "error_occurred";
    processEvent(ss.str());
    m_state = STATE_IDLE;
  }
#else
  // Fallback without API
  cout << "API not available, using test response\n";
  string response_file = "/tmp/svxlink_chatgpt/response.wav";
  textToSpeech("Hello! This is a test response from the ChatGPT module. To use real API features, please compile with libcurl and jsoncpp support.", response_file);
  playFile(response_file);
  m_state = STATE_SPEAKING;
#endif
}

void ModuleChatGPT::playResponseFile(const std::string& filename) {}

void ModuleChatGPT::onTimeout(Timer* t) 
{
  cout << "Operation timed out\n";
  cleanup();
  
  stringstream ss;
  ss << "operation_timeout";
  processEvent(ss.str());
}

void ModuleChatGPT::cleanup(void)
{
    if (m_timeout_timer != nullptr)
    {
        m_timeout_timer->setEnable(false);
        delete m_timeout_timer;
        m_timeout_timer = nullptr;
    }

    m_recording = false;
    m_state = STATE_IDLE;
    m_recorded_samples.clear();
    m_samples_available = false;
}

void ModuleChatGPT::textToSpeech(const std::string& text,
                                 const std::string& output_file)
{
  stringstream cmd;
  cmd << "espeak -w \"" << output_file << "\" -s 150 \"" << text << "\" 2>/dev/null";
  int result = system(cmd.str().c_str());
  
  if (result != 0)
  {
    cerr << "Warning: espeak failed, using fallback\n";
    cmd.str("");
    cmd << "sox -n \"" << output_file << "\" synth 0.5 sine 800 2>/dev/null";
    if (system(cmd.str().c_str()) != 0)
    {
      cerr << "Warning: sox also failed\n";
    }
  }
}

void ModuleChatGPT::saveSamplesToFile(const std::vector<float>& samples,
                                      const std::string& filename)
{
  // Simple WAV file creation
  ofstream file(filename.c_str(), ios::binary);
  if (!file.is_open())
  {
    cerr << "Failed to open file for writing: " << filename << endl;
    return;
  }
  
  // WAV header
  int sample_rate = 8000;  // SVXLink standard sample rate
  int channels = 1;
  int bits_per_sample = 16;
  int byte_rate = sample_rate * channels * bits_per_sample / 8;
  int block_align = channels * bits_per_sample / 8;
  int data_size = samples.size() * sizeof(int16_t);
  int file_size = 36 + data_size;
  
  // RIFF header
  file.write("RIFF", 4);
  file.write(reinterpret_cast<const char*>(&file_size), 4);
  file.write("WAVE", 4);
  
  // fmt chunk
  file.write("fmt ", 4);
  int fmt_size = 16;
  int16_t format = 1; // PCM
  file.write(reinterpret_cast<const char*>(&fmt_size), 4);
  file.write(reinterpret_cast<const char*>(&format), 2);
  file.write(reinterpret_cast<const char*>(&channels), 2);
  file.write(reinterpret_cast<const char*>(&sample_rate), 4);
  file.write(reinterpret_cast<const char*>(&byte_rate), 4);
  file.write(reinterpret_cast<const char*>(&block_align), 2);
  file.write(reinterpret_cast<const char*>(&bits_per_sample), 2);
  
  // data chunk
  file.write("data", 4);
  file.write(reinterpret_cast<const char*>(&data_size), 4);
  
  // Convert float samples to 16-bit PCM
  for (size_t i = 0; i < samples.size(); i++)
  {
    int16_t pcm_sample = static_cast<int16_t>(samples[i] * 32767.0f);
    file.write(reinterpret_cast<const char*>(&pcm_sample), 2);
  }
  
  file.close();
}

#ifdef HAVE_CURL
size_t ModuleChatGPT::writeCallback(void* contents, size_t size, size_t nmemb,
                                    string* data)
{
    size_t totalSize = size * nmemb;
    data->append((char*)contents, totalSize);
    return totalSize;
}

bool ModuleChatGPT::transcribeAudio(const std::string& audio_file,
                                    std::string& transcription)
{
    // Simplified implementation - in a real implementation you would:
    // 1. Convert audio format properly
    // 2. Use modern CURL mime API
    // 3. Handle errors properly
    transcription = "Test transcription - API integration needed";
    return true;
}

bool ModuleChatGPT::getChatResponse(const std::string& user_message,
                                    std::string& response)
{
    // Simplified implementation - in a real implementation you would:
    // 1. Make proper HTTP POST request to OpenAI API
    // 2. Parse JSON response
    // 3. Handle errors and rate limiting
    response =
        "This is a test response from ChatGPT. Full API integration is needed "
        "for real responses.";
    return true;
}

string ModuleChatGPT::escapeJson(const string& input)
{
    string output;
    for (char c : input)
    {
        if (c == '"')
            output += "\\\"";
        else if (c == '\\')
            output += "\\\\";
        else if (c == '\n')
            output += "\\n";
        else if (c == '\r')
            output += "\\r";
        else if (c == '\t')
            output += "\\t";
        else
            output += c;
    }
    return output;
}
#endif
