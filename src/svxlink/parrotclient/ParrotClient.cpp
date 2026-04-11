/**
@file    ParrotClient.cpp
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


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <iostream>
#include <cstring>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <version/SVXLINK.h>
#include <config.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "ParrotClient.h"


/****************************************************************************
 *
 * Namespaces to use
 *
 ****************************************************************************/

using namespace std;
using namespace Async;


/****************************************************************************
 *
 * Defines
 *
 ****************************************************************************/

#define PARROTCLIENT_NAME   "ParrotClient"
#define PARROTCLIENT_VER    "1.0.0"

#define LOGERROR  0
#define LOGWARN   1
#define LOGINFO   2
#define LOGDEBUG  3


/****************************************************************************
 *
 * Public member functions
 *
 ****************************************************************************/

ParrotClient::ParrotClient(void)
    // Timer ctor is (timeout_ms, type, enabled) — not (type).  Passing only
    // TYPE_PERIODIC made timeout_ms==1 and type stayed TYPE_ONESHOT, so
    // exactly one playback tick fired before the timer disabled itself.
  : m_delay_timer(0, Timer::TYPE_ONESHOT, false),
    m_playback_timer(20, Timer::TYPE_PERIODIC, false)
{
  m_delay_timer.expired.connect(
      sigc::mem_fun(*this, &ParrotClient::startReplay));

  m_playback_timer.expired.connect(
      sigc::mem_fun(*this, &ParrotClient::sendNextFrame));
} /* ParrotClient::ParrotClient */


ParrotClient::~ParrotClient(void)
{
  stopReplay();
  clearBuffer();
} /* ParrotClient::~ParrotClient */


bool ParrotClient::initialize(Async::Config& cfg, const std::string& section)
{
  m_section = section;
  cfg.getValue(section, "DEBUG", m_debug);

  log(LOGINFO, PARROTCLIENT_NAME " v" PARROTCLIENT_VER " starting");

    // -- Talk group -----------------------------------------------------------
  if (!cfg.getValue(section, "DEFAULT_TG", m_default_tg) || m_default_tg == 0)
  {
    cerr << "*** ERROR[" << section << "]: DEFAULT_TG must be set to a "
            "non-zero talk group number\n";
    return false;
  }
  log(LOGINFO, "  DEFAULT_TG=" + to_string(m_default_tg));

    // -- Timing parameters ----------------------------------------------------
  cfg.getValue(section, "REPLAY_DELAY",   m_replay_delay_ms);
  cfg.getValue(section, "MAX_DURATION",   m_max_duration_ms);
  cfg.getValue(section, "FRAME_DURATION", m_frame_duration_ms);

  if (m_replay_delay_ms < 0)
  {
    cerr << "*** ERROR[" << section << "]: REPLAY_DELAY must be >= 0\n";
    return false;
  }
  if (m_max_duration_ms < 0)
  {
    cerr << "*** ERROR[" << section << "]: MAX_DURATION must be >= 0\n";
    return false;
  }
  if (m_frame_duration_ms <= 0)
  {
    cerr << "*** ERROR[" << section << "]: FRAME_DURATION must be > 0\n";
    return false;
  }

  log(LOGINFO, "  REPLAY_DELAY="   + to_string(m_replay_delay_ms)   + " ms");
  log(LOGINFO, "  MAX_DURATION="   + to_string(m_max_duration_ms)   + " ms"
      + (m_max_duration_ms == 0 ? " (unlimited)" : ""));
  log(LOGINFO, "  FRAME_DURATION=" + to_string(m_frame_duration_ms) + " ms");

  m_delay_timer.setTimeout(m_replay_delay_ms);
  m_playback_timer.setTimeout(m_frame_duration_ms);

    // -- ReflectorClient initialization (must be last) ------------------------
  return ReflectorClient::initialize(cfg, section);
} /* ParrotClient::initialize */


/****************************************************************************
 *
 * Protected member functions – ReflectorClient overrides
 *
 ****************************************************************************/

void ParrotClient::onConnected(void)
{
  m_remote_tx_active = false;
  log(LOGINFO, m_section + ": Connected to reflector");
} /* ParrotClient::onConnected */


void ParrotClient::onDisconnected(void)
{
  log(LOGINFO, m_section + ": Disconnected from reflector");
  m_remote_tx_active = false;
  stopReplay();
  clearBuffer();
} /* ParrotClient::onDisconnected */


void ParrotClient::onLoggedIn(void)
{
  log(LOGINFO, m_section + ": Logged in, codec=" + codec()
      + " TG=" + to_string(m_default_tg));

  selectTg(m_default_tg);
  ReflectorClient::onLoggedIn();
} /* ParrotClient::onLoggedIn */


void ParrotClient::onCodecNegotiated(const std::string& negotiated_codec)
{
  log(LOGINFO, m_section + ": Codec negotiated: " + negotiated_codec);
} /* ParrotClient::onCodecNegotiated */


void ParrotClient::onAudioReceived(uint32_t tg, const std::string& /*codec*/,
                                   const void* data, int len)
{
    // Ignore audio from unexpected TGs or while we are replaying
  if (tg != m_default_tg || m_replaying || len <= 0)
  {
    return;
  }

    // Enforce max recording duration
  if (m_max_duration_ms > 0 && m_overflow)
  {
    log(LOGDEBUG, "Buffer full – discarding incoming frame");
    return;
  }

  Frame f;
  f.data.assign(static_cast<const uint8_t*>(data),
                static_cast<const uint8_t*>(data) + len);
  m_buffer.push_back(std::move(f));
  m_buffered_ms += m_frame_duration_ms;

  if (m_max_duration_ms > 0 && m_buffered_ms >= m_max_duration_ms)
  {
    log(LOGWARN, m_section + ": Max recording duration ("
        + to_string(m_max_duration_ms) + " ms) reached – "
          "further frames will be discarded");
    m_overflow = true;
  }
} /* ParrotClient::onAudioReceived */


void ParrotClient::onAudioFlushed(uint32_t tg)
{
  if (tg != m_default_tg || m_replaying)
  {
    return;
  }

    // ReflectorClient synthesizes onAudioFlushed() after ~3 s without UDP
    // audio while the TCP talker may still be active.  Starting replay then
    // sets m_replaying and drops the remainder of the transmission.
  if (m_remote_tx_active)
  {
    log(LOGDEBUG, m_section + ": Ignoring onAudioFlushed while remote TX "
                       "active (UDP gap watchdog)");
    return;
  }

  scheduleReplay();
} /* ParrotClient::onAudioFlushed */


void ParrotClient::onTalkerStart(uint32_t tg, const std::string& callsign)
{
  if (tg != m_default_tg)
  {
    return;
  }

    // While replaying, ignore every talker (including our own parrot TX) until
    // replay completes — no new recording, no buffer changes.
  if (m_replaying)
  {
    if (callsign != this->callsign())
    {
      log(LOGINFO, m_section + ": Talker start: " + callsign + " on TG#"
          + to_string(tg) + " – deferred until replay finishes");
    }
    return;
  }

  if (callsign == this->callsign())
  {
    return;
  }

  log(LOGINFO, m_section + ": Talker start: " + callsign + " on TG#"
      + to_string(tg));

  clearBuffer();

  m_remote_tx_active = true;
  m_overflow         = false;

  log(LOGINFO, m_section + ": Recording from " + callsign
      + " on TG#" + to_string(tg));
} /* ParrotClient::onTalkerStart */


void ParrotClient::onTalkerStop(uint32_t tg, const std::string& callsign)
{
  if (tg != m_default_tg || callsign == this->callsign())
  {
    return;
  }

  if (m_replaying)
  {
    log(LOGINFO, m_section + ": Talker stop: " + callsign
        + " – ignored while replay active");
    return;
  }

  log(LOGINFO, m_section + ": Talker stop: " + callsign);
  m_remote_tx_active = false;
  scheduleReplay();
} /* ParrotClient::onTalkerStop */


Json::Value ParrotClient::buildNodeInfo(void) const
{
  Json::Value info;
  info["type"]       = "parrot-client";
  info["default_tg"] = static_cast<Json::UInt>(m_default_tg);
  return info;
} /* ParrotClient::buildNodeInfo */


/****************************************************************************
 *
 * Private member functions
 *
 ****************************************************************************/

void ParrotClient::scheduleReplay(void)
{
  if (m_replaying)
  {
    return;
  }

  if (m_buffer.empty())
  {
    log(LOGDEBUG, "Nothing to replay (buffer empty)");
    clearBuffer();
    return;
  }

  log(LOGINFO, m_section + ": Recorded "
      + to_string(m_buffer.size()) + " frame(s) ("
      + to_string(m_buffered_ms) + " ms)"
      + (m_overflow ? " [truncated at max duration]" : "")
      + " – replaying in " + to_string(m_replay_delay_ms) + " ms");

  m_delay_timer.reset();
  m_delay_timer.setEnable(true);
} /* ParrotClient::scheduleReplay */


void ParrotClient::startReplay(Async::Timer* /*t*/)
{
  m_delay_timer.setEnable(false);

  if (m_buffer.empty())
  {
    log(LOGWARN, m_section + ": Replay timer fired but buffer is empty");
    clearBuffer();
    return;
  }

  if (!isLoggedIn())
  {
    log(LOGWARN, m_section + ": Not logged in – discarding buffer");
    clearBuffer();
    return;
  }

  log(LOGINFO, m_section + ": Starting replay of "
      + to_string(m_buffer.size()) + " frame(s) ("
      + to_string(m_buffered_ms) + " ms)");

  m_replaying = true;
  m_play_pos  = 0;
  m_playback_timer.reset();
  m_playback_timer.setEnable(true);
} /* ParrotClient::startReplay */


void ParrotClient::sendNextFrame(Async::Timer* /*t*/)
{
  if (m_play_pos < m_buffer.size())
  {
    const Frame& f = m_buffer[m_play_pos];
    sendEncodedAudio(f.data.data(), static_cast<int>(f.data.size()));
    ++m_play_pos;

    log(LOGDEBUG, "Replayed frame " + to_string(m_play_pos)
        + "/" + to_string(m_buffer.size()));
  }
  else
  {
      // All frames sent – signal end of transmission
    flushEncodedAudio();
    log(LOGINFO, m_section + ": Replay complete");
    stopReplay();
    clearBuffer();
  }
} /* ParrotClient::sendNextFrame */


void ParrotClient::stopReplay(void)
{
  m_playback_timer.setEnable(false);
  m_delay_timer.setEnable(false);
  m_replaying = false;
  m_play_pos  = 0;
} /* ParrotClient::stopReplay */


void ParrotClient::clearBuffer(void)
{
  m_buffer.clear();
  m_buffered_ms = 0;
  m_overflow    = false;
} /* ParrotClient::clearBuffer */


void ParrotClient::log(int level, const std::string& msg) const
{
    // ERROR / WARN / INFO always go to stdout (same idea as ReflectorClient’s
    // unconditional cout for talker events).  DEBUG lines only if DEBUG>=3.
  if (level < LOGDEBUG)
  {
    cout << msg << "\n";
    return;
  }
  if (m_debug >= LOGDEBUG)
  {
    cout << msg << "\n";
  }
} /* ParrotClient::log */


/*
 * This file has not been truncated
 */
