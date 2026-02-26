/**
@file    SubAudioFskDecoder.h
@brief   A subaudible FSK user identification decoder
@author  Rui Barreiros
@date    2026-02-24

This file contains a decoder class that detects a subaudible FSK user
identification burst transmitted at the start of each PTT event. The
identification frame consists of a preamble, sync word, 24-bit unit ID
(DMR-compatible), 4-bit flags, and a CRC-8 error check byte.

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

#ifndef SUB_AUDIO_FSK_DECODER_INCLUDED
#define SUB_AUDIO_FSK_DECODER_INCLUDED


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <cstdint>
#include <string>
#include <sigc++/sigc++.h>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncConfig.h>
#include <AsyncAudioSink.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "Goertzel.h"


/****************************************************************************
 *
 * Forward declarations
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Namespace
 *
 ****************************************************************************/

//namespace MyNameSpace
//{


/****************************************************************************
 *
 * Forward declarations of classes inside of the declared namespace
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Defines & typedefs
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Exported Global Variables
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Class definitions
 *
 ****************************************************************************/

/**
@brief  A subaudible FSK user identification decoder
@author Rui Barreiros
@date   2026-02-24

This class decodes a subaudible FSK user identification burst embedded in an
FM transmission below the voice band (typically 260-276 Hz). Two Goertzel
detectors run in parallel on successive bit-period windows. A state machine
recovers preamble → sync → 24-bit unit ID → 4-bit flags → CRC-8, and emits
the idDecoded signal when a complete frame has been processed.

The decoder is always connected to unfiltered (fullband) audio but only
processes bits after activate(true) is called (e.g. on squelch open). On
activate(false) the state machine resets ready for the next PTT event.

Frame structure (52 bits total at the configured baud rate):
  [ Preamble 0xAA: 8 bits ][ Sync 0x7E: 8 bits ]
  [ Unit ID: 24 bits ][ Flags: 4 bits ][ CRC-8: 8 bits ]

Configuration variables (in the [RxN] section of svxlink.conf):
  FSK_ID_ENABLE          - Set to 1 to enable (default: 0)
  FSK_ID_F0              - Space frequency Hz, bit=0 (default: 260.0)
  FSK_ID_F1              - Mark  frequency Hz, bit=1 (default: 276.0)
  FSK_ID_BAUD            - Baud rate (default: 100)
  FSK_ID_MIN_ENERGY      - Minimum Goertzel energy noise gate (default: 0.001)
  FSK_ID_OPEN_WITHOUT_ID - Open squelch even if no valid ID received (default: 1)
  FSK_ID_DEBUG           - Verbose debug output (default: 0)
*/
class SubAudioFskDecoder : public sigc::trackable, public Async::AudioSink
{
  public:
    /**
     * @brief   Constructor
     * @param   cfg     Reference to the configuration object
     * @param   rx_name The receiver name used to look up configuration
     */
    SubAudioFskDecoder(Async::Config &cfg, const std::string &rx_name);

    /**
     * @brief   Disallow copy construction
     */
    SubAudioFskDecoder(const SubAudioFskDecoder &) = delete;

    /**
     * @brief   Disallow copy assignment
     */
    SubAudioFskDecoder &operator=(const SubAudioFskDecoder &) = delete;

    /**
     * @brief   Destructor
     */
    ~SubAudioFskDecoder(void);

    /**
     * @brief   Initialize the decoder
     * @return  Returns \em true on success or else \em false
     *
     * Must be called once after construction before any samples are written.
     * Reads configuration and prepares the Goertzel detectors.
     */
    bool initialize(void);

    /**
     * @brief   Activate or deactivate the decoder
     * @param   active Set to \em true when PTT/squelch activity starts,
     *                 \em false when it ends.
     *
     * When set to true the state machine starts accumulating bits. When set
     * to false the state machine resets to IDLE, discarding any partial frame,
     * ready for the next PTT event.
     */
    void activate(bool active);

    /**
     * @brief   Returns whether the decoder is currently active
     * @return  Returns \em true if currently active
     */
    bool isActive(void) const { return m_active; }

    /**
     * @brief   Returns the last successfully decoded unit ID
     * @return  Returns the unit ID (24-bit DMR-compatible value) or 0 if none
     *          has been decoded since the last reset.
     */
    uint32_t lastId(void) const { return m_last_id; }

    /**
     * @brief   Returns whether the last frame had a valid CRC
     * @return  Returns \em true if the last decoded frame passed CRC
     */
    bool lastIdValid(void) const { return m_last_id_valid; }

    /**
     * @brief   Returns whether squelch should open without a valid ID
     * @return  Returns \em true if configured to open without ID
     */
    bool openWithoutId(void) const { return m_open_without_id; }

    /**
     * @brief   Write samples into the decoder
     * @param   samples Buffer containing the samples
     * @param   count   Number of samples in the buffer
     * @return  Returns the number of samples consumed
     *
     * Implementation of Async::AudioSink::writeSamples.
     */
    virtual int writeSamples(const float *samples, int count) override;

    /**
     * @brief   Flush previously written samples
     *
     * Implementation of Async::AudioSink::flushSamples.
     */
    virtual void flushSamples(void) override
    {
      sourceAllSamplesFlushed();
    }

    /**
     * @brief   Signal emitted when a complete FSK ID frame has been processed
     * @param   id    The decoded 24-bit unit ID (0 if not found/invalid CRC)
     * @param   valid \em true if the CRC check passed
     *
     * This signal is emitted once per PTT event at most — as soon as a
     * complete frame (valid or not) has been received. Listeners may use the
     * unit ID for logging, access control, or squelch gating.
     */
    sigc::signal<void(uint32_t, bool)> idDecoded;

  private:
    /**
     * @brief   Decoder state machine states
     */
    enum class State
    {
      IDLE,       //!< Waiting for activate()
      PREAMBLE,   //!< Looking for alternating preamble pattern
      SYNC,       //!< Looking for sync byte 0x7E
      DATA,       //!< Collecting data bits (ID + flags + CRC)
      DONE        //!< Frame complete; waiting for deactivation
    };

    Async::Config &       m_cfg;
    std::string           m_rx_name;
    bool                  m_active              = false;
    State                 m_state               = State::IDLE;

    // Configuration
    float                 m_f0                  = 260.0f;
    float                 m_f1                  = 276.0f;
    unsigned              m_baud_rate           = 100;
    float                 m_min_energy          = 0.001f;
    bool                  m_open_without_id     = true;
    bool                  m_debug               = false;

    // Goertzel detectors
    Goertzel              m_det_f0;
    Goertzel              m_det_f1;

    // Bit timing
    int                   m_samples_per_bit     = 80;  // 8000/100
    int                   m_sample_count        = 0;
    float                 m_passband_energy     = 0.0f;

    // Bit stream accumulator
    uint8_t               m_current_byte        = 0;
    int                   m_bit_count           = 0;

    // Preamble detection
    int                   m_preamble_transitions= 0;
    int                   m_last_bit            = -1;

    // Frame data
    uint32_t              m_unit_id             = 0;
    uint8_t               m_flags               = 0;
    uint8_t               m_received_crc        = 0;
    int                   m_data_bits_collected = 0;

    // Total data bits to collect: 24 (ID) + 4 (flags) + 8 (CRC) = 36
    static const int      DATA_BITS_TOTAL       = 36;
    // Minimum preamble transitions before we accept
    static const int      MIN_PREAMBLE_TRANSITIONS = 6;

    // Results
    uint32_t              m_last_id             = 0;
    bool                  m_last_id_valid       = false;

    /**
     * @brief   Process a single decoded bit through the state machine
     * @param   bit The decoded bit value (0 or 1)
     */
    void processBit(int bit);

    /**
     * @brief   Process one complete bit window worth of samples
     */
    void processWindow(void);

    /**
     * @brief   Compute CRC-8 over a data buffer
     * @param   data Pointer to data bytes
     * @param   len  Number of bytes
     * @return  Returns the CRC-8 value
     *
     * Uses polynomial 0x07, initial value 0xFF.
     */
    static uint8_t crc8(const uint8_t *data, size_t len);

    /**
     * @brief   Reset the state machine to IDLE
     */
    void resetStateMachine(void);

};  /* class SubAudioFskDecoder */


//} /* namespace */

#endif /* SUB_AUDIO_FSK_DECODER_INCLUDED */



/*
 * This file has not been truncated
 */
