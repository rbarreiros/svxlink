/**
@file    ParrotClient.h
@brief   Standalone SvxReflector parrot client
@author  Rui Barreiros / CR7BPM

\verbatim
SvxLink - A Multi Purpose Voice Services System for Ham Radio Use
Copyright (C) 2003-2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
\endverbatim
*/

#ifndef PARROT_CLIENT_H
#define PARROT_CLIENT_H


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <cstdint>
#include <string>
#include <vector>
#include <deque>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncConfig.h>
#include <AsyncTimer.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "../reflectorclient/ReflectorClient.h"


/****************************************************************************
 *
 * Class definitions
 *
 ****************************************************************************/

/**
@brief  Standalone SvxReflector parrot client

Connects to an SvxReflector, records incoming encoded audio frames, and
replays them back to the same talk group after a configurable delay.
Frames beyond a configurable maximum recording duration are silently
discarded; any transmission that has already exceeded the limit is still
replayed up to the point where the limit was hit.

Configuration keys (in addition to all ReflectorClient keys):

  DEFAULT_TG      – Talk group to monitor and replay on (required, > 0)
  MONITOR_TGS     – Additional TGs to monitor (space/comma list)
  REPLAY_DELAY    – Milliseconds to wait after a transmission ends before
                    replaying it (default: 1000)
  MAX_DURATION    – Maximum milliseconds of audio to buffer per transmission.
                    Frames arriving after this limit is reached are dropped.
                    0 = unlimited (default: 30000)
  FRAME_DURATION  – Duration of a single encoded audio frame in milliseconds.
                    This controls the pacing of the playback.  Should match
                    the codec frame size; 20 ms is correct for OPUS and GSM
                    at 8 kHz (default: 20)
  DEBUG           – Verbosity 0=errors 1=warn 2=info 3=debug (default: 0)
*/
class ParrotClient : public ReflectorClient
{
  public:
    ParrotClient(void);
    virtual ~ParrotClient(void) override;

    /**
     * @brief   Initialize the parrot client
     * @param   cfg     Configuration object
     * @param   section Config section name
     * @return  true on success
     */
    bool initialize(Async::Config& cfg, const std::string& section);

  protected:
    // -- ReflectorClient overrides --------------------------------------------

    void onConnected(void) override;
    void onDisconnected(void) override;
    void onLoggedIn(void) override;

    void onCodecNegotiated(const std::string& codec) override;

    void onAudioReceived(uint32_t tg, const std::string& codec,
                         const void* data, int len) override;
    void onAudioFlushed(uint32_t tg) override;

    void onTalkerStart(uint32_t tg, const std::string& callsign) override;
    void onTalkerStop(uint32_t tg, const std::string& callsign) override;

    Json::Value buildNodeInfo(void) const override;

  private:
    // -- Configuration --------------------------------------------------------
    std::string     m_section;
    uint32_t        m_default_tg      = 0;
    int             m_replay_delay_ms = 1000;
    int             m_max_duration_ms = 30000;
    int             m_frame_duration_ms = 20;
    int             m_debug           = 0;

    // -- Audio buffer ---------------------------------------------------------
    struct Frame
    {
      std::vector<uint8_t> data;
    };

    std::deque<Frame>   m_buffer;           // buffered encoded frames
    int                 m_buffered_ms  = 0; // accumulated duration in buffer
    bool                m_overflow     = false; // max duration exceeded
    bool                m_recording    = false; // currently recording a TX
    bool                m_replaying    = false; // currently replaying

    // -- Playback state -------------------------------------------------------
    std::size_t         m_play_pos     = 0;
    Async::Timer        m_delay_timer;      // fires after REPLAY_DELAY
    Async::Timer        m_playback_timer;   // fires every FRAME_DURATION ms

    // -- Helpers --------------------------------------------------------------
    ParrotClient(const ParrotClient&)            = delete;
    ParrotClient& operator=(const ParrotClient&) = delete;

    void startReplay(Async::Timer* t = nullptr);
    void sendNextFrame(Async::Timer* t = nullptr);
    void stopReplay(void);
    void clearBuffer(void);

    void log(int level, const std::string& msg) const;

}; /* class ParrotClient */


#endif /* PARROT_CLIENT_H */


/*
 * This file has not been truncated
 */
