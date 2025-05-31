//
// AudioBuffer.cpp - Implementation
//
#include "AudioBuffer.h"
#include <algorithm>

using namespace std;
using namespace VoiceAI;

AudioBuffer::AudioBuffer(size_t max_samples)
    : m_max_samples(max_samples),
      m_sample_rate(16000),
      m_min_duration_ms(1000) {
    m_buffer.reserve(max_samples);
}

void AudioBuffer::addSamples(const vector<float>& samples) {
    lock_guard<mutex> lock(m_mutex);
    
    m_last_add_time = chrono::steady_clock::now();
    
    // Add new samples
    m_buffer.insert(m_buffer.end(), samples.begin(), samples.end());
    
    // Keep buffer within limits
    if (m_buffer.size() > m_max_samples) {
        size_t excess = m_buffer.size() - m_max_samples;
        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + excess);
    }
}

vector<float> AudioBuffer::getAudioData() {
    lock_guard<mutex> lock(m_mutex);
    return m_buffer;
}

bool AudioBuffer::hasEnoughData() const {
    lock_guard<mutex> lock(m_mutex);
    
    // Check if we have minimum required samples
    size_t min_samples = getMinSamples();
    if (m_buffer.size() < min_samples) {
        return false;
    }
    
    // Check if enough time has passed since last audio input
    auto now = chrono::steady_clock::now();
    auto silence_duration = chrono::duration_cast<chrono::milliseconds>(now - m_last_add_time);
    
    return silence_duration.count() > 500; // 500ms of silence indicates end of speech
}

void AudioBuffer::clear() {
    lock_guard<mutex> lock(m_mutex);
    m_buffer.clear();
}

size_t AudioBuffer::getMinSamples() const {
    return (m_sample_rate * m_min_duration_ms) / 1000;
}
