/**
@file	 RnnoiseFilter.cpp
@brief   Audio processor wrapping the RNNoise noise suppression library
@date	 2026-10-21
*/

#include <rnnoise.h>
#include <cmath>

#include "RnnoiseFilter.h"

namespace
{
    // SvxLink uses floats in the range -1.0..1.0. RNNoise expects floats with
    // the amplitude of 16 bit integers.
  const float SCALE = 32768.0f;
};

RnnoiseFilter::RnnoiseFilter(void)
  : m_st(rnnoise_create(nullptr)), m_frame_size(rnnoise_get_frame_size()),
    m_pos(0), m_vad_prob(0.0f), m_mix(1.0f), m_gain(1.0f)
{
  m_in_buf.assign(m_frame_size, 0.0f);
  m_out_buf.assign(m_frame_size, 0.0f);
} /* RnnoiseFilter::RnnoiseFilter */


RnnoiseFilter::~RnnoiseFilter(void)
{
  if (m_st != nullptr)
  {
    rnnoise_destroy(m_st);
  }
} /* RnnoiseFilter::~RnnoiseFilter */


void RnnoiseFilter::processSamples(float *dest, const float *src, int count)
{
    // Exactly "count" samples must be produced for "count" samples consumed.
    // The oldest processed sample is output first and then replaced by the new
    // input sample at the same position. When a frame has been collected it is
    // processed in one go and becomes the output for the next frame period.
    // This gives a constant delay of one frame and no extra buffering.

    for (int i = 0; i < count; ++i)
    {
        // m_in_buf[m_pos] still holds the input sample from one frame ago, which
        // is exactly the dry counterpart of m_out_buf[m_pos] (same delay).
        dest[i] = (m_mix * m_out_buf[m_pos] + (1.0f - m_mix) * m_in_buf[m_pos])
                    * m_gain / SCALE;
        m_in_buf[m_pos] = src[i] * SCALE;
        if (++m_pos == m_frame_size)
        {
            m_pos = 0;
            m_vad_prob = rnnoise_process_frame(m_st, m_out_buf.data(),
                                                m_in_buf.data());
        }
    }

} /* RnnoiseFilter::processSamples */

void RnnoiseFilter::setGain(float db)
{
  m_gain = powf(10.0f, db / 20.0f);
} /* RnnoiseFilter::setGain */
