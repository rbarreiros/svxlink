#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <sys/stat.h>

// SVXLink includes
#include "ModuleChatGPT.h"
#include <AsyncAudioRecorder.h>
#include <AsyncAudioPlayer.h>
#include <AsyncTimer.h>

// Optional library includes
#ifdef HAVE_JSONCPP
#include <json/json.h>
#endif

#ifdef HAVE_CURL
#include <curl/curl.h>
#endif

using namespace std;
using namespace Async;

PLUGIN_EXPORT Module* module_init(void *dl_handle, Logic *logic, const string& cfg_name)
{
  return new ModuleChatGPT(dl_handle, logic, cfg_name);
}

ModuleChatGPT::ModuleChatGPT(void *dl_handle, Logic *logic, const std::string& cfg_name)
  : Module(dl_handle, logic, cfg_name),
    m_state(STATE_IDLE),
    m_keyword("chatgpt"),
    m_chat_model("gpt-3.5-turbo"),
    m_timeout(30000),
    m_max_tokens(150),
    m_temperature(0.7),
    m_recording(false),
    m_recorder(0),
    m_player(0),
    m_timeout_timer(0)
{
  cout << "\tModule " << name() << " v1.0.0 starting...\n";
}

ModuleChatGPT::~ModuleChatGPT(void)
{
  cleanup();
}

bool ModuleChatGPT::initialize(void)
{
  string value;
  
  // Read configuration values
  if (cfg().getValue(cfgName(), "KEYWORD", value))
  {
    m_keyword = value;
  }
  
  if (!cfg().getValue(cfgName(), "API_KEY", m_api_key))
  {
    cerr << "*** ERROR: Config variable " << cfgName() << "/API_KEY not set\n";
    cerr << "*** Please obtain an OpenAI API key and set it in the configuration\n";
    return false;
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
  system("mkdir -p /tmp/svxlink_chatgpt");
  m_temp_audio_file = "/tmp/svxlink_chatgpt/recording.wav";
  
  // Initialize timeout timer
  m_timeout_timer = new Timer(m_timeout);
  m_timeout_timer->expired.connect(mem_fun(*this, &ModuleChatGPT::onTimeout));
  
#ifdef HAVE_CURL
  curl_global_init(CURL_GLOBAL_DEFAULT);
  cout << "\tModule " << name() << " initialized with API support\n";
#else
  cout << "\tModule " << name() << " initialized without API support (libcurl not available)\n";
#endif
  
  return Module::initialize();
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
  
#ifdef HAVE_CURL
  curl_global_cleanup();
#endif
}

void ModuleChatGPT::cleanup(void)
{
  if (m_recorder)
  {
    removeSink(m_recorder);
    delete m_recorder;
    m_recorder = 0;
  }
  
  if (m_player)
  {
    removeSource(m_player);
    delete m_player;
    m_player = 0;
  }
  
  if (m_timeout_timer)
  {
    m_timeout_timer->setEnable(false);
  }
  
  m_recording = false;
  m_state = STATE_IDLE;
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

void ModuleChatGPT::startRecording(void)
{
  if (m_recording) return;
  
  cout << "Starting audio recording...\n";
  
  m_recorder = new AudioRecorder;
  m_recorder->setFilename(m_temp_audio_file);
  addSink(m_recorder, true);
  
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
  
  if (m_recorder)
  {
    removeSink(m_recorder);
    delete m_recorder;
    m_recorder = 0;
  }
  
  m_recording = false;
  m_timeout_timer->setEnable(false);
  
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
  struct stat st;
  if (stat(m_temp_audio_file.c_str(), &st) != 0 || st.st_size < 1000)
  {
    cout << "Audio file too small or missing\n";
    playResponse("Sorry, I didn't receive any audio. Please try again.");
    return;
  }
  
  sendToAPI(m_temp_audio_file);
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
      playResponse(chat_response);
    }
    else
    {
      playResponse("Sorry, I couldn't get a response from ChatGPT.");
    }
  }
  else
  {
    playResponse("Sorry, I couldn't understand your audio. Please try again.");
  }
#else
  // Fallback without API
  cout << "API not available, using test response\n";
  playResponse("Hello! This is a test response from the ChatGPT module. To use real API features, please compile with libcurl and jsoncpp support.");
#endif
}

bool ModuleChatGPT::transcribeAudio(const std::string& audio_file, std::string& transcription)
{
#ifdef HAVE_CURL
  CURL *curl;
  CURLcode res;
  string response_data;
  
  curl = curl_easy_init();
  if (!curl) return false;
  
  // Convert audio to proper format for API (simplified - in practice you'd want proper conversion)
  string converted_file = "/tmp/svxlink_chatgpt/converted.mp3";
  stringstream convert_cmd;
  convert_cmd << "ffmpeg -i \"" << audio_file << "\" -ar 16000 -ac 1 -ab 32k \"" 
              << converted_file << "\" -y 2>/dev/null";
  if (system(convert_cmd.str().c_str()) != 0)
  {
    // Fallback to direct use if ffmpeg fails
    converted_file = audio_file;
  }
  
  struct curl_httppost *formpost = NULL;
  struct curl_httppost *lastptr = NULL;
  struct curl_slist *headerlist = NULL;
  
  // Add auth header
  string auth_header = "Authorization: Bearer " + m_api_key;
  headerlist = curl_slist_append(headerlist, auth_header.c_str());
  
  // Add form fields
  curl_formadd(&formpost, &lastptr,
               CURLFORM_COPYNAME, "file",
               CURLFORM_FILE, converted_file.c_str(),
               CURLFORM_CONTENTTYPE, "audio/wav",
               CURLFORM_END);
               
  curl_formadd(&formpost, &lastptr,
               CURLFORM_COPYNAME, "model",
               CURLFORM_COPYCONTENTS, "whisper-1",
               CURLFORM_END);
  
  curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/audio/transcriptions");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);
  curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
  
  res = curl_easy_perform(curl);
  
  curl_easy_cleanup(curl);
  curl_formfree(formpost);
  curl_slist_free_all(headerlist);
  
  if (res == CURLE_OK)
  {
#ifdef HAVE_JSONCPP
    Json::Reader reader;
    Json::Value response_json;
    
    if (reader.parse(response_data, response_json))
    {
      if (response_json.isMember("text"))
      {
        transcription = response_json["text"].asString();
        return true;
      }
    }
#else
    // Simple text extraction without JSON parsing
    size_t start = response_data.find("\"text\":\"");
    if (start != string::npos)
    {
      start += 8;
      size_t end = response_data.find("\"", start);
      if (end != string::npos)
      {
        transcription = response_data.substr(start, end - start);
        return true;
      }
    }
#endif
  }
  
  return false;
#else
  return false;
#endif
}

bool ModuleChatGPT::getChatResponse(const std::string& user_message, std::string& response)
{
#ifdef HAVE_CURL
  CURL *curl;
  CURLcode res;
  string response_data;
  
  curl = curl_easy_init();
  if (!curl) return false;
  
  // Prepare JSON payload
  stringstream json_payload;
  json_payload << "{"
               << "\"model\":\"" << m_chat_model << "\","
               << "\"messages\":[{\"role\":\"user\",\"content\":\"" << escapeJson(user_message) << "\"}],"
               << "\"max_tokens\":" << m_max_tokens << ","
               << "\"temperature\":" << m_temperature
               << "}";
  
  string payload = json_payload.str();
  
  struct curl_slist *headers = NULL;
  string auth_header = "Authorization: Bearer " + m_api_key;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, auth_header.c_str());
  
  curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/chat/completions");
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
  
  res = curl_easy_perform(curl);
  
  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);
  
  if (res == CURLE_OK)
  {
#ifdef HAVE_JSONCPP
    Json::Reader reader;
    Json::Value response_json;
    
    if (reader.parse(response_data, response_json))
    {
      if (response_json.isMember("choices") && 
          response_json["choices"].isArray() &&
          response_json["choices"].size() > 0)
      {
        response = response_json["choices"][0]["message"]["content"].asString();
        return true;
      }
    }
#else
    // Simple text extraction without JSON parsing
    size_t start = response_data.find("\"content\":\"");
    if (start != string::npos)
    {
      start += 11;
      size_t end = response_data.find("\"", start);
      if (end != string::npos)
      {
        response = response_data.substr(start, end - start);
        return true;
      }
    }
#endif
  }
  
  return false;
#else
  return false;
#endif
}

void ModuleChatGPT::playResponse(const std::string& response_text)
{
  cout << "Playing response: " << response_text << endl;
  m_state = STATE_SPEAKING;
  
  string output_file = "/tmp/svxlink_chatgpt/response.wav";
  textToSpeech(response_text, output_file);
  
  m_player = new AudioPlayer;
  m_player->setFilename(output_file);
  m_player->allSamplesFlushed.connect(
      mem_fun(*this, &ModuleChatGPT::allSamplesFlushed));
  
  addSource(m_player);
  m_player->play();
}

void ModuleChatGPT::textToSpeech(const std::string& text, const std::string& output_file)
{
  stringstream cmd;
  cmd << "espeak -w \"" << output_file << "\" -s 150 \"" << text << "\" 2>/dev/null";
  int result = system(cmd.str().c_str());
  
  if (result != 0)
  {
    cerr << "Warning: espeak failed, using fallback\n";
    cmd.str("");
    cmd << "sox -n \"" << output_file << "\" synth 0.5 sine 800 2>/dev/null";
    system(cmd.str().c_str());
  }
}

void ModuleChatGPT::onTimeout(void)
{
  cout << "Operation timed out\n";
  cleanup();
  
  stringstream ss;
  ss << "operation_timeout";
  processEvent(ss.str());
}

string ModuleChatGPT::escapeJson(const string& input)
{
  string output;
  for (char c : input)
  {
    if (c == '"') output += "\\\"";
    else if (c == '\\') output += "\\\\";
    else if (c == '\n') output += "\\n";
    else if (c == '\r') output += "\\r";
    else if (c == '\t') output += "\\t";
    else output += c;
  }
  return output;
}

#ifdef HAVE_CURL
size_t ModuleChatGPT::writeCallback(void *contents, size_t size, size_t nmemb, string *data)
{
  size_t totalSize = size * nmemb;
  data->append((char*)contents, totalSize);
  return totalSize;
}
#endif

// Required module interface functions
void ModuleChatGPT::resumeOutput(void)
{
  // Implementation for audio output resumption
}

void ModuleChatGPT::allSamplesFlushed(void)
{
  cout << "Response playback finished\n";
  
  if (m_player)
  {
    removeSource(m_player);
    delete m_player;
    m_player = 0;
  }
  
  m_state = STATE_IDLE;
  
  stringstream ss;
  ss << "ready_for_next";
  processEvent(ss.str());
}

int ModuleChatGPT::writeSamples(const float *samples, int count)
{
  return count;
}

void ModuleChatGPT::flushSamples(void)
{
  // Flush any pending audio samples
}

void ModuleChatGPT::allMsgsWritten(void)
{
  allSamplesFlushed();
}
