#ifndef MODULE_CHATGPT_INCLUDED
#define MODULE_CHATGPT_INCLUDED

#include <Module.h>
#include <string>

// Forward declarations
namespace Async {
  class AudioRecorder;
  class AudioPlayer;
  class Timer;
}

class ModuleChatGPT : public Module
{
  public:
    ModuleChatGPT(void *dl_handle, Logic *logic, const std::string& cfg_name);
    ~ModuleChatGPT(void);
    const char *compiledForVersion(void) const { return SVXLINK_VERSION; }

  protected:
    void resumeOutput(void);
    void allSamplesFlushed(void);
    int writeSamples(const float *samples, int count);
    void flushSamples(void);
    bool initialize(void);
    void activateInit(void);
    void deactivateCleanup(void);
    bool dtmfDigitReceived(char digit, int duration);
    void dtmfCmdReceived(const std::string& cmd);
    void dtmfCmdReceivedWhenIdle(const std::string& cmd);
    void squelchOpen(bool is_open);
    void allMsgsWritten(void);

  private:
    enum State {
      STATE_IDLE,
      STATE_LISTENING,
      STATE_PROCESSING,
      STATE_SPEAKING
    };

    State                   m_state;
    std::string             m_keyword;
    std::string             m_api_key;
    std::string             m_chat_model;
    int                     m_timeout;
    int                     m_max_tokens;
    double                  m_temperature;
    bool                    m_recording;
    std::string             m_temp_audio_file;
    
    Async::AudioRecorder   *m_recorder;
    Async::AudioPlayer     *m_player;
    Async::Timer           *m_timeout_timer;
    
    void startRecording(void);
    void stopRecording(void);
    void processAudioRequest(void);
    void sendToAPI(const std::string& audio_file);
    void playResponse(const std::string& response_text);
    void onTimeout(void);
    void cleanup(void);
    void textToSpeech(const std::string& text, const std::string& output_file);
    
    // HTTP/API helper functions
    static size_t writeCallback(void *contents, size_t size, size_t nmemb, std::string *data);
    bool transcribeAudio(const std::string& audio_file, std::string& transcription);
    bool getChatResponse(const std::string& user_message, std::string& response);
    std::string escapeJson(const std::string& input);
};

#endif // MODULE_CHATGPT_INCLUDED