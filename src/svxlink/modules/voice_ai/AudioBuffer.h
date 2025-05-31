//
// AudioBuffer.h - Audio buffering for processing
//
#ifndef AUDIO_BUFFER_H
#define AUDIO_BUFFER_H

#include <vector>
#include <mutex>
#include <chrono>

namespace VoiceAI {

class AudioBuffer {
public:
    AudioBuffer(size_t max_samples);
    
    void addSamples(const std::vector<float>& samples);
    std::vector<float> getAudioData();
    bool hasEnoughData() const;
    void clear();
    
    void setMinDuration(int ms) { m_min_duration_ms = ms; }
    void setSampleRate(int rate) { m_sample_rate = rate; }
    
private:
    mutable std::mutex m_mutex;
    std::vector<float> m_buffer;
    size_t m_max_samples;
    int m_sample_rate;
    int m_min_duration_ms;
    std::chrono::steady_clock::time_point m_last_add_time;
    
    size_t getMinSamples() const;
};

} // namespace VoiceAI

#endif // AUDIO_BUFFER_H
