/**
@file	 RnnoiseFilter.h
@brief   Audio processor wrapping the RNNoise noise suppression library
@date	 2026-10-21

RNNoise only works on 48kHz audio in blocks of exactly 480 samples (10ms). This
class is therefore a 1:1 sample rate processor that expects 48kHz input. It
does its own re-blocking, which adds a fixed delay of one frame (10ms).
Use AudioInterpolator/AudioDecimator around it when the audio pipe runs at
another sample rate.
*/

#ifndef RNNOISE_FILTER_INCLUDED
#define RNNOISE_FILTER_INCLUDED

#include <vector>
#include <AsyncAudioProcessor.h>

struct DenoiseState;

class RnnoiseFilter : public Async::AudioProcessor
{
  public:
    static const int REQUIRED_SAMPLE_RATE = 48000;

    RnnoiseFilter(void);
    ~RnnoiseFilter(void);

    /**
     * @brief  Check if the RNNoise state was successfully created
     * @return Returns true if the filter is usable
     */
    bool isValid(void) const { return m_st != nullptr; }

    /**
     * @brief  Get the voice activity probability (0..1) from the last frame
     */
    float vadProbability(void) const { return m_vad_prob; }

    /**
     * @brief  Set the wet/dry mix
     * @param  wet 0.0 = only original signal, 1.0 = only RNNoise output
     */
    void setMix(float wet)
    {
      m_mix = (wet < 0.0f) ? 0.0f : ((wet > 1.0f) ? 1.0f : wet);
    }

    /**
     * @brief  Set output makeup gain
     * @param  db Gain in dB applied after mixing
     */
    void setGain(float db);

  protected:
    virtual void processSamples(float *dest, const float *src, int count);

  private:
    DenoiseState       *m_st;
    int                 m_frame_size;
    int                 m_pos;
    float               m_vad_prob;
    std::vector<float>  m_in_buf;
    std::vector<float>  m_out_buf;
    float               m_mix;    // add
    float               m_gain;   // add (linear)

    RnnoiseFilter(const RnnoiseFilter&);
    RnnoiseFilter& operator=(const RnnoiseFilter&);

};  /* class RnnoiseFilter */

#endif /* RNNOISE_FILTER_INCLUDED */
