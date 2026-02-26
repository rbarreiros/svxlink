/**
@file    SubAudioFskDecoder.cpp
@brief   A subaudible FSK user identification decoder
@author  Rui Barreiros
@date    2026-02-24

\verbatim
SvxLink - A Multi Purpose Voice Services System for Ham Radio Use
Copyright (C) 2003-2026 Tobias Blomberg / SM0SVX

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
\endverbatim
*/


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <iostream>
#include <iomanip>
#include <cstring>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncConfig.h>
#include <common.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "SubAudioFskDecoder.h"


/****************************************************************************
 *
 * Namespaces to use
 *
 ****************************************************************************/

using namespace std;
using namespace Async;


/****************************************************************************
 *
 * Defines & typedefs
 *
 ****************************************************************************/

  // Internal audio sample rate used by SvxLink
#ifndef INTERNAL_SAMPLE_RATE
#define INTERNAL_SAMPLE_RATE 8000
#endif


/****************************************************************************
 *
 * Local class definitions
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Prototypes
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Exported Global Variables
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Local Global Variables
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Public member functions
 *
 ****************************************************************************/

SubAudioFskDecoder::SubAudioFskDecoder(Config &cfg, const std::string &rx_name)
  : m_cfg(cfg), m_rx_name(rx_name)
{
} /* SubAudioFskDecoder::SubAudioFskDecoder */


SubAudioFskDecoder::~SubAudioFskDecoder(void)
{
} /* SubAudioFskDecoder::~SubAudioFskDecoder */


bool SubAudioFskDecoder::initialize(void)
{
  m_cfg.getValue(m_rx_name, "FSK_ID_F0", m_f0);
  m_cfg.getValue(m_rx_name, "FSK_ID_F1", m_f1);
  m_cfg.getValue(m_rx_name, "FSK_ID_BAUD", m_baud_rate);
  m_cfg.getValue(m_rx_name, "FSK_ID_MIN_ENERGY", m_min_energy);
  m_cfg.getValue(m_rx_name, "FSK_ID_OPEN_WITHOUT_ID", m_open_without_id);
  m_cfg.getValue(m_rx_name, "FSK_ID_DEBUG", m_debug);

  if (m_f0 <= 0.0f || m_f1 <= 0.0f || m_f0 == m_f1)
  {
    cerr << "*** ERROR: " << m_rx_name
         << ": FSK_ID_F0 and FSK_ID_F1 must be positive and different\n";
    return false;
  }

  if (m_baud_rate == 0)
  {
    cerr << "*** ERROR: " << m_rx_name
         << ": FSK_ID_BAUD must be greater than 0\n";
    return false;
  }

  m_samples_per_bit = INTERNAL_SAMPLE_RATE / static_cast<int>(m_baud_rate);
  if (m_samples_per_bit < 4)
  {
    cerr << "*** ERROR: " << m_rx_name
         << ": FSK_ID_BAUD results in too few samples per bit ("
         << m_samples_per_bit << "). Lower the baud rate.\n";
    return false;
  }

  m_det_f0.initialize(m_f0, INTERNAL_SAMPLE_RATE);
  m_det_f1.initialize(m_f1, INTERNAL_SAMPLE_RATE);

  if (m_debug)
  {
    cout << m_rx_name << ": FSK ID decoder initialized"
         << " F0=" << m_f0 << "Hz F1=" << m_f1 << "Hz"
         << " baud=" << m_baud_rate
         << " samples_per_bit=" << m_samples_per_bit << "\n";
  }

  return true;

} /* SubAudioFskDecoder::initialize */


void SubAudioFskDecoder::activate(bool active)
{
  if (active && !m_active)
  {
    if (m_debug)
    {
      cout << m_rx_name << ": FSK ID decoder activated\n";
    }
    resetStateMachine();
    m_state = State::PREAMBLE;
  }
  else if (!active && m_active)
  {
    if (m_debug)
    {
      cout << m_rx_name << ": FSK ID decoder deactivated\n";
    }
    resetStateMachine();
  }
  m_active = active;

} /* SubAudioFskDecoder::activate */


int SubAudioFskDecoder::writeSamples(const float *samples, int count)
{
  if (!m_active || m_state == State::IDLE || m_state == State::DONE)
  {
    return count;
  }

  for (int i = 0; i < count; ++i)
  {
    float s = samples[i];
    m_det_f0.calc(s);
    m_det_f1.calc(s);
    m_passband_energy += s * s;
    ++m_sample_count;

    if (m_sample_count >= m_samples_per_bit)
    {
      processWindow();
      m_det_f0.reset();
      m_det_f1.reset();
      m_passband_energy = 0.0f;
      m_sample_count = 0;
    }
  }

  return count;

} /* SubAudioFskDecoder::writeSamples */


/****************************************************************************
 *
 * Private member functions
 *
 ****************************************************************************/

void SubAudioFskDecoder::processWindow(void)
{
  float energy_f0 = m_det_f0.magnitudeSquared();
  float energy_f1 = m_det_f1.magnitudeSquared();
  float max_energy = (energy_f0 > energy_f1) ? energy_f0 : energy_f1;

  // Noise gate: if neither tone has sufficient energy, skip this window
  if (max_energy < m_min_energy)
  {
    if (m_debug && m_state == State::PREAMBLE)
    {
      cout << m_rx_name << ": FSK noise gate (energy=" << max_energy << ")\n";
    }
    return;
  }

  int bit = (energy_f1 > energy_f0) ? 1 : 0;

  if (m_debug)
  {
    cout << m_rx_name << ": FSK bit=" << bit
         << " e0=" << energy_f0 << " e1=" << energy_f1
         << " state=" << static_cast<int>(m_state) << "\n";
  }

  processBit(bit);

} /* SubAudioFskDecoder::processWindow */


void SubAudioFskDecoder::processBit(int bit)
{
  switch (m_state)
  {
    case State::PREAMBLE:
    {
      // Count transitions in the alternating preamble 10101010...
      if (m_last_bit == -1)
      {
        m_last_bit = bit;
      }
      else if (bit != m_last_bit)
      {
        ++m_preamble_transitions;
        m_last_bit = bit;

          // Accumulate preamble byte in parallel to detect sync
        m_current_byte = (m_current_byte << 1) | static_cast<uint8_t>(bit);
        ++m_bit_count;

        if (m_preamble_transitions >= MIN_PREAMBLE_TRANSITIONS)
        {
          if (m_debug)
          {
            cout << m_rx_name << ": FSK preamble locked ("
                 << m_preamble_transitions << " transitions)\n";
          }
          // Reset byte accumulator and move to SYNC detection
          m_current_byte = 0;
          m_bit_count = 0;
          m_state = State::SYNC;
        }
      }
      else
      {
        // Two consecutive identical bits: not a valid preamble, reset count
        // but keep current bit as the new reference
        m_preamble_transitions = 0;
        m_current_byte = 0;
        m_bit_count = 0;
        m_last_bit = bit;
      }
      break;
    }

    case State::SYNC:
    {
      m_current_byte = (m_current_byte << 1) | static_cast<uint8_t>(bit);
      ++m_bit_count;

      if (m_bit_count == 8)
      {
        if (m_current_byte == 0x7E)
        {
          if (m_debug)
          {
            cout << m_rx_name << ": FSK sync word found\n";
          }
          m_current_byte = 0;
          m_bit_count = 0;
          m_data_bits_collected = 0;
          m_unit_id = 0;
          m_flags = 0;
          m_received_crc = 0;
          m_state = State::DATA;
        }
        else
        {
          // No sync yet, keep sliding the window (shift in the new bit)
          m_bit_count = 8; // stays at 8, we just shifted
          // bit_count is already 8, just let it slide
        }
      }
      break;
    }

    case State::DATA:
    {
      // Collect 36 bits: 24 ID + 4 flags + 8 CRC
      int bit_pos = m_data_bits_collected;

      if (bit_pos < 24)
      {
        // 24-bit unit ID, MSB first
        m_unit_id = (m_unit_id << 1) | static_cast<uint32_t>(bit);
      }
      else if (bit_pos < 28)
      {
        // 4-bit flags
        m_flags = (m_flags << 1) | static_cast<uint8_t>(bit);
      }
      else
      {
        // 8-bit CRC
        m_received_crc = (m_received_crc << 1) | static_cast<uint8_t>(bit);
      }

      ++m_data_bits_collected;

      if (m_data_bits_collected == DATA_BITS_TOTAL)
      {
        // Frame complete — validate CRC
        uint8_t crc_data[4];
        crc_data[0] = static_cast<uint8_t>((m_unit_id >> 16) & 0xFF);
        crc_data[1] = static_cast<uint8_t>((m_unit_id >> 8)  & 0xFF);
        crc_data[2] = static_cast<uint8_t>( m_unit_id        & 0xFF);
        crc_data[3] = m_flags & 0x0F;

        uint8_t computed_crc = crc8(crc_data, sizeof(crc_data));
        bool valid = (computed_crc == m_received_crc);

        if (valid)
        {
          m_last_id = m_unit_id;
          m_last_id_valid = true;
        }
        else
        {
          m_last_id = 0;
          m_last_id_valid = false;
        }

        if (m_debug)
        {
          cout << m_rx_name << ": FSK frame complete"
               << " id=0x" << hex << setw(6) << setfill('0') << m_unit_id
               << " flags=0x" << static_cast<unsigned>(m_flags)
               << " crc_rx=0x" << setw(2) << static_cast<unsigned>(m_received_crc)
               << " crc_calc=0x" << setw(2) << static_cast<unsigned>(computed_crc)
               << " " << (valid ? "VALID" : "INVALID") << dec << "\n";
        }

        if (valid)
        {
          cout << m_rx_name << ": FSK ID decoded: unit_id=" << m_unit_id
               << " flags=" << static_cast<unsigned>(m_flags) << "\n";
        }
        else
        {
          cerr << m_rx_name << ": FSK ID CRC error — frame discarded\n";
        }

        idDecoded(m_unit_id & 0x00FFFFFFU, valid);
        m_state = State::DONE;
      }
      break;
    }

    case State::IDLE:
    case State::DONE:
      break;
  }

} /* SubAudioFskDecoder::processBit */


void SubAudioFskDecoder::resetStateMachine(void)
{
  m_state                 = State::IDLE;
  m_sample_count          = 0;
  m_passband_energy       = 0.0f;
  m_current_byte          = 0;
  m_bit_count             = 0;
  m_preamble_transitions  = 0;
  m_last_bit              = -1;
  m_unit_id               = 0;
  m_flags                 = 0;
  m_received_crc          = 0;
  m_data_bits_collected   = 0;
  m_det_f0.reset();
  m_det_f1.reset();

} /* SubAudioFskDecoder::resetStateMachine */


uint8_t SubAudioFskDecoder::crc8(const uint8_t *data, size_t len)
{
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; ++i)
  {
    crc ^= data[i];
    for (int j = 0; j < 8; ++j)
    {
      if (crc & 0x80)
      {
        crc = static_cast<uint8_t>((crc << 1) ^ 0x07);
      }
      else
      {
        crc <<= 1;
      }
    }
  }
  return crc;

} /* SubAudioFskDecoder::crc8 */


/*
 * This file has not been truncated
 */
