/**
@file    UsrpClient.cpp
@brief   Standalone USRP ↔ SvxReflector bridge
@author  Rui Barreiros / CR7BPM

\verbatim
SvxLink - A Multi Purpose Voice Services System for Ham Radio Use
Copyright (C) 2003-2026 Tobias Blomberg / SM0SVX

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

#include <sys/time.h>
#include <cstring>
#include <cmath>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cassert>
#include <vector>
#include <set>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncAudioInterpolator.h>
#include <AsyncAudioDecimator.h>
#include <AsyncAudioClipper.h>
#include <AsyncAudioCompressor.h>
#include <version/SVXLINK.h>
#include <config.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "UsrpClient.h"


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

#define USRPCLIENT_NAME    "UsrpClient"
#define USRPCLIENT_VER     "1.0.0"

#define LOGERROR  0
#define LOGWARN   1
#define LOGINFO   2
#define LOGDEBUG  3

// multirate FIR coefficients reused from svxlink trx
#include "../trx/multirate_filter_coeff.h"


/****************************************************************************
 *
 * Public member functions
 *
 ****************************************************************************/

UsrpClient::UsrpClient(void)
  : m_flush_timer(3000, Timer::TYPE_ONESHOT, false),
    m_tx_watchdog(5000, Timer::TYPE_ONESHOT, false)
{
  timerclear(&m_last_audio_ts);
  m_flush_timer.expired.connect(
      sigc::mem_fun(*this, &UsrpClient::flushTimeout));
  m_tx_watchdog.expired.connect(
      sigc::mem_fun(*this, &UsrpClient::txWatchdogExpired));
} /* UsrpClient::UsrpClient */


UsrpClient::~UsrpClient(void)
{
  delete m_usrp_sock;
  m_usrp_sock = nullptr;
  delete m_dec;
  m_dec = nullptr;
  delete m_s16_dec;
  m_s16_dec = nullptr;
  delete m_enc;
  m_enc = nullptr;
} /* UsrpClient::~UsrpClient */


bool UsrpClient::initialize(Async::Config& cfg, const std::string& section)
{
  cfg.getValue(section, "DEBUG", m_debug);
  m_section = section;

  if (!cfg.getValue(section, "USRP_HOST", m_usrp_host) || m_usrp_host.empty())
  {
    cerr << "*** ERROR[" << section
         << "]: USRP_HOST not configured" << endl;
    return false;
  }

  cfg.getValue(section, "USRP_TX_PORT", m_usrp_tx_port);
  cfg.getValue(section, "USRP_RX_PORT", m_usrp_rx_port);

  log(LOGINFO, "  USRP_HOST="    + m_usrp_host);
  log(LOGINFO, "  USRP_TX_PORT=" + to_string(m_usrp_tx_port));
  log(LOGINFO, "  USRP_RX_PORT=" + to_string(m_usrp_rx_port));

  cfg.getValue(section, "DMRID",      m_dmrid);
  cfg.getValue(section, "RPTID",      m_rptid);
  cfg.getValue(section, "DEFAULT_TG", m_default_tg);

  {
    vector<string> monitor_tokens;
    if (!cfg.getValue(section, "MONITOR_TGS", monitor_tokens, true))
    {
      cerr << "*** ERROR[" << section
           << "]: Illegal MONITOR_TGS (use space/comma-separated TG numbers)\n";
      return false;
    }
    set<uint32_t> monitor_tgs;
    for (const auto& tok : monitor_tokens)
    {
      if (tok.empty()) { continue; }
      uint32_t tg = 0;
      istringstream iss(tok);
      iss >> tg;
      if (iss.fail() || tg == 0)
      {
        cerr << "*** WARNING[" << section
             << "]: Ignoring invalid MONITOR_TGS entry \"" << tok << "\"\n";
        continue;
      }
      monitor_tgs.insert(tg);
    }
    monitorTgs(monitor_tgs);
    selectTg(m_default_tg);

    log(LOGINFO, "  DEFAULT_TG=" + to_string(m_default_tg)
        + " (outgoing USRP→reflector)");
    if (!monitor_tgs.empty())
    {
      string mon;
      for (auto it = monitor_tgs.begin(); it != monitor_tgs.end(); ++it)
      {
        if (it != monitor_tgs.begin()) mon += ", ";
        mon += to_string(*it);
      }
      log(LOGINFO, "  MONITOR_TGS=" + mon + " (incoming reflector→USRP)");
    }
    else
    {
      log(LOGINFO, "  MONITOR_TGS=(none — no extra TGs monitored)");
    }
  }

  std::string tmp;
  if (cfg.getValue(section, "DEFAULT_CC", tmp)) { m_cc = atoi(tmp.c_str()) & 0xff; }
  if (cfg.getValue(section, "DEFAULT_TS", tmp)) { m_ts = atoi(tmp.c_str()) & 0xff; }

  {
    double tx_preamp_db = 0.0;
    cfg.getValue(section, "USRP_TX_PREAMP", tx_preamp_db);
    m_tx_preamp = (tx_preamp_db == 0.0)
                      ? 1.0f
                      : static_cast<float>(pow(10.0, tx_preamp_db / 20.0));
    if (tx_preamp_db != 0.0)
      log(LOGINFO, "  USRP_TX_PREAMP=" + to_string(tx_preamp_db)
          + " dB (linear=" + to_string(m_tx_preamp) + ")");
  }
  {
    double rx_preamp_db = 0.0;
    cfg.getValue(section, "USRP_RX_PREAMP", rx_preamp_db);
    m_rx_preamp = (rx_preamp_db == 0.0)
                      ? 1.0f
                      : static_cast<float>(pow(10.0, rx_preamp_db / 20.0));
    if (rx_preamp_db != 0.0)
      log(LOGINFO, "  USRP_RX_PREAMP=" + to_string(rx_preamp_db)
          + " dB (linear=" + to_string(m_rx_preamp) + ")");
  }
  {
    bool le = true;
    cfg.getValue(section, "USRP_AUDIO_LE", le);
    m_usrp_audio_le = le;
    log(LOGINFO, std::string("  USRP_AUDIO_LE=")
        + (m_usrp_audio_le
               ? "true (apply ntohs — UsrpLogic / normal USRP default)"
               : "false (raw samples after unpack, no ntohs)"));
  }

  m_usrp_sock = new UdpSocket(m_usrp_rx_port);
  m_usrp_sock->dataReceived.connect(
      sigc::mem_fun(*this, &UsrpClient::usrpDatagramReceived));

  return ReflectorClient::initialize(cfg, section);
} /* UsrpClient::initialize */


/****************************************************************************
 *
 * Protected member functions
 *
 ****************************************************************************/

void UsrpClient::onConnected(void)
{
  log(LOGINFO, m_section + ": Connected to reflector");
} /* UsrpClient::onConnected */


void UsrpClient::onDisconnected(void)
{
  log(LOGINFO, m_section + ": Disconnected from reflector");
  if (m_ptt_on)
  {
    sendUsrpStop();
  }
} /* UsrpClient::onDisconnected */


void UsrpClient::onLoggedIn(void)
{
  string mon_log;
  for (uint32_t tg : monitoredTgs())
  {
    if (!mon_log.empty()) mon_log += ", ";
    mon_log += to_string(tg);
  }
  if (mon_log.empty()) mon_log = "(none)";

  log(LOGINFO, m_section + ": Logged in, codec=" + codec()
      + " selected_TG=" + to_string(selectedTg())
      + " monitor_TGs=" + mon_log);

  if (selectedTg() == 0)
  {
    log(LOGWARN, m_section + ": WARNING: DEFAULT_TG is 0 — outgoing audio "
        "from USRP will not be routed anywhere on the reflector. "
        "Set DEFAULT_TG in the config.");
  }

  ReflectorClient::onLoggedIn();
} /* UsrpClient::onLoggedIn */


void UsrpClient::onCodecNegotiated(const std::string& negotiated_codec)
{
  log(LOGINFO, m_section + ": Codec negotiated: " + negotiated_codec);
  if (!setupAudioPipeline(negotiated_codec))
  {
    cerr << "*** ERROR[" << m_section
         << "]: Failed to set up audio pipeline for codec: "
         << negotiated_codec << endl;
  }
} /* UsrpClient::onCodecNegotiated */


void UsrpClient::onAudioReceived(uint32_t tg, const std::string& /*codec*/,
                                 const void* data, int len)
{
  if (m_usrp_ptt_on)
  {
    if (tg == m_default_tg) return;
  }

  if (m_dec != nullptr)
  {
    m_dec->writeEncodedSamples(const_cast<void*>(data), len);
  }
} /* UsrpClient::onAudioReceived */


void UsrpClient::onAudioFlushed(uint32_t tg)
{
  if (m_dec != nullptr)
  {
    m_dec->flushEncodedSamples();
  }
  timerclear(&m_last_audio_ts);
} /* UsrpClient::onAudioFlushed */


void UsrpClient::onAllSamplesFlushed(void)
{
  if (m_enc != nullptr)
  {
    m_enc->allEncodedSamplesFlushed();
  }
} /* UsrpClient::onAllSamplesFlushed */


void UsrpClient::onTalkerStart(uint32_t tg, const std::string& callsign)
{
  if (m_usrp_ptt_on)
  {
    return;
  }

  log(LOGINFO, m_section + ": Talker start on TG #" + to_string(tg)
      + ": " + callsign);

  sendUsrpMeta(callsign);
} /* UsrpClient::onTalkerStart */


void UsrpClient::onTalkerStop(uint32_t tg, const std::string& callsign)
{
  if (m_usrp_ptt_on)
  {
    return;
  }

  log(LOGINFO, m_section + ": Talker stop on TG #" + to_string(tg)
      + ": " + callsign);
  sendUsrpMeta("");

  if (m_dec != nullptr)
  {
    m_dec->flushEncodedSamples();
  }
} /* UsrpClient::onTalkerStop */


Json::Value UsrpClient::buildNodeInfo(void) const
{
  Json::Value info;
  info["type"] = "usrp-client";
  return info;
} /* UsrpClient::buildNodeInfo */


/****************************************************************************
 *
 * Private member functions
 *
 ****************************************************************************/

void UsrpClient::usrpDatagramReceived(const IpAddress& addr, uint16_t port,
                                      void* buf, int count)
{
  if (count < USRP_HEADER_LEN)
  {
    log(LOGWARN, "[USRP-RX] Short datagram (" + to_string(count)
        + " bytes < " + to_string(USRP_HEADER_LEN) + "), ignored");
    return;
  }

  log(LOGDEBUG, "[USRP-RX] " + to_string(count) + " bytes from "
      + addr.toString() + ":" + to_string(port));

  stringstream ss;
  ss.write(reinterpret_cast<const char*>(buf), count);

  UsrpHeaderMsg hdr;
  if (!hdr.unpack(ss))
  {
    log(LOGERROR, "[USRP-RX] Failed to unpack USRP header");
    return;
  }

  const uint32_t utype  = hdr.type();
  const bool     keyup  = hdr.keyup();
  const uint32_t seq    = hdr.seq();

  log(LOGDEBUG, "[USRP-RX] type=" + to_string(utype)
      + " keyup=" + (keyup ? "1" : "0")
      + " seq=" + to_string(seq)
      + " size=" + to_string(count));

  if (utype == USRP_TYPE_VOICE)
  {
    if (!keyup)
    {
      log(LOGINFO, "[USRP-RX] PTT off (keyup=0, seq=" + to_string(seq) + ")");
      handleStreamStop();
    }
    else
    {
        // Re-parse as full audio frame
      stringstream sa;
      sa.write(reinterpret_cast<const char*>(buf), count);
      UsrpAudioMsg amsg;
      if (!amsg.unpack(sa))
      {
        log(LOGERROR, "[USRP-RX] Failed to unpack USRP audio frame");
        return;
      }
      log(LOGDEBUG, "[USRP-RX] Voice frame seq=" + to_string(amsg.seq())
          + " keyup=" + to_string(amsg.keyup())
          + " payload=" + to_string(USRP_AUDIO_FRAME_LEN) + " samples");
      handleVoiceFrame(&amsg.audioData(), count);
    }
  }
  else if (utype == USRP_TYPE_TEXT)
  {
      // Check for TLV metadata (first data byte == 0x08)
    stringstream sm;
    sm.write(reinterpret_cast<const char*>(buf), count);
    UsrpMetaTextMsg mtxt;
    if (!mtxt.unpack(sm)) { return; }

    if (mtxt.isTlv())
    {
      stringstream stlv;
      stlv.write(reinterpret_cast<const char*>(buf), count);
      UsrpTlvMetaMsg tlv;
      if (tlv.unpack(stlv))
      {
        log(LOGINFO, "[USRP-RX] TLV meta: callsign=" + tlv.getCallsign()
            + " tg=" + to_string(tlv.getTg())
            + " dmrid=" + to_string(tlv.getDmrId()));
      }
    }
    else
    {
      log(LOGDEBUG, "[USRP-RX] TEXT frame (non-TLV), ignored");
    }
  }
  else if (utype == USRP_TYPE_PING)
  {
    log(LOGDEBUG, "[USRP-RX] PING received");
  }
  else
  {
    log(LOGDEBUG, "[USRP-RX] Unknown frame type " + to_string(utype) + ", ignored");
  }
} /* UsrpClient::usrpDatagramReceived */


void UsrpClient::handleVoiceFrame(const void* audio_array, int /*count*/)
{
  gettimeofday(&m_last_audio_ts, nullptr);

  if (m_s16_dec == nullptr)
  {
    log(LOGWARN, "[TX] Voice frame dropped — audio pipeline not ready yet "
        "(codec not negotiated)");
    return;
  }

  if (!isLoggedIn())
  {
    log(LOGWARN, "[TX] Voice frame dropped — not logged in to reflector");
    return;
  }

  if (selectedTg() == 0)
  {
    log(LOGWARN, "[TX] Voice frame dropped — no TG selected (DEFAULT_TG is 0)");
    return;
  }

  const bool is_first_frame = !m_usrp_ptt_on;

  if (is_first_frame)
  {
    m_usrp_ptt_on = true;
    log(LOGINFO, "[TX] PTT on — USRP → reflector TG " + to_string(selectedTg()));
    m_tx_watchdog.setEnable(true);
  }
  else
  {
    m_tx_watchdog.reset();
  }

  const auto* samples =
      reinterpret_cast<const array<int16_t, FRAME_SAMPLES>*>(audio_array);

  array<int16_t, FRAME_SAMPLES> host_samples{};
  float   peak = 0.0f, sum_sq = 0.0f;
  int16_t diag[5]{};
  for (int i = 0; i < FRAME_SAMPLES; ++i)
  {
    int16_t raw   = (*samples)[i];
    int16_t fixed = m_usrp_audio_le ? static_cast<int16_t>(ntohs(raw)) : raw;

    if (i < 5) diag[i] = fixed;

    float fval = static_cast<float>(fixed);
    sum_sq += fval * fval;
    float afval = fval < 0.0f ? -fval : fval;
    if (afval > peak) peak = afval;

    float f = fval / 32768.0f * m_tx_preamp;
    if      (f >  1.0f) f =  1.0f;
    else if (f < -1.0f) f = -1.0f;
    host_samples[i] = static_cast<int16_t>(f * 32767.0f);
  }

  if (m_debug >= LOGINFO && is_first_frame)
  {
    float rms = sqrtf(sum_sq / FRAME_SAMPLES);
    log(LOGINFO, "[TX] First frame digest:"
        " peak=" + to_string(static_cast<int>(peak))
        + " rms=" + to_string(static_cast<int>(rms))
        + " (full-scale=32768)"
        + " samples[0..4]="
        + to_string(diag[0]) + "," + to_string(diag[1]) + ","
        + to_string(diag[2]) + "," + to_string(diag[3]) + ","
        + to_string(diag[4]) + " (m_usrp_audio_le="
        + (m_usrp_audio_le ? "true" : "false") + ")");
  }

  if (m_debug >= LOGDEBUG)
  {
      float rms = sqrtf(sum_sq / FRAME_SAMPLES);
      log(LOGDEBUG, "[TX] Audio stats: peak=" + to_string(static_cast<int>(peak))
          + " rms=" + to_string(static_cast<int>(rms)));
  }

  log(LOGDEBUG, "[TX] Feeding " + to_string(FRAME_SAMPLES)
      + " S16 samples into pipeline"
      + (m_tx_preamp != 1.0f
           ? " (preamp=" + to_string(m_tx_preamp) + ")"
           : ""));

  m_s16_dec->writeEncodedSamples(host_samples.data(),
                                 sizeof(int16_t) * FRAME_SAMPLES);
} /* UsrpClient::handleVoiceFrame */


void UsrpClient::handleStreamStop(void)
{
  m_tx_watchdog.setEnable(false);

  if (!m_usrp_ptt_on)
  {
    log(LOGDEBUG, "[TX] PTT-off received but was not transmitting, ignored");
    return;
  }

  m_usrp_ptt_on = false;
  log(LOGINFO, "[TX] PTT off — flushing TX chain to reflector");

  if (m_s16_dec != nullptr)
  {
    m_s16_dec->flushEncodedSamples();
  }
  m_meta_sent = false;
  timerclear(&m_last_audio_ts);
} /* UsrpClient::handleStreamStop */


void UsrpClient::txWatchdogExpired(Async::Timer* /*t*/)
{
  m_tx_watchdog.setEnable(false);
  if (m_usrp_ptt_on)
  {
    log(LOGWARN, "[TX] Watchdog: no USRP frames for 5 s, forcing PTT off");
    m_usrp_ptt_on = false;
    if (m_s16_dec != nullptr)
    {
      m_s16_dec->flushEncodedSamples();
    }
    m_meta_sent = false;
  }
} /* UsrpClient::txWatchdogExpired */


void UsrpClient::txEncoderOutput(const void* buf, int count)
{
  log(LOGDEBUG, "[TX] Encoder produced " + to_string(count)
      + " bytes → sending to reflector TG " + to_string(selectedTg()));
  ReflectorClient::sendEncodedAudio(buf, count);
} /* UsrpClient::txEncoderOutput */


void UsrpClient::txEncoderFlushed(void)
{
  log(LOGINFO, "[TX] Encoder flushed → sending MsgUdpFlushSamples to reflector");
  ReflectorClient::flushEncodedAudio();
} /* UsrpClient::txEncoderFlushed */


void UsrpClient::allEncodedSamplesFlushed(void)
{
  log(LOGINFO, "[RX] Decoder chain fully flushed → sending USRP PTT-off");
  sendUsrpStop();
} /* UsrpClient::allEncodedSamplesFlushed */


void UsrpClient::flushTimeout(Async::Timer* /*t*/)
{
  m_flush_timer.setEnable(false);
  log(LOGWARN, "[RX] Flush timeout — forcing allEncodedSamplesFlushed on encoder");
  if (m_enc != nullptr)
  {
    m_enc->allEncodedSamplesFlushed();
  }
} /* UsrpClient::flushTimeout */


void UsrpClient::sendUsrpAudio(const void* pcm16le, int byte_count)
{
  if (!isLoggedIn()) { return; }

  if (!m_meta_sent)
  {
    log(LOGINFO, "[RX] Sending USRP TLV meta frame (callsign=" + callsign() + ")");
    sendUsrpMeta(callsign());
    m_meta_sent = true;
  }

  const int16_t* in = reinterpret_cast<const int16_t*>(pcm16le);
  const int      n  = byte_count / static_cast<int>(sizeof(int16_t));

  log(LOGDEBUG, "[RX] " + to_string(n) + " S16 samples (" + to_string(byte_count)
      + " bytes) from decoder → USRP framing buffer (stored="
      + to_string(m_tx_stored) + ")");

  vector<int16_t> rx_buf;
  if (m_rx_preamp != 1.0f)
  {
    rx_buf.resize(n);
    for (int i = 0; i < n; ++i)
    {
      float f = static_cast<float>(in[i]) / 32768.0f * m_rx_preamp;
      if      (f >  1.0f) f =  1.0f;
      else if (f < -1.0f) f = -1.0f;
      rx_buf[i] = static_cast<int16_t>(f * 32767.0f);
    }
    in = rx_buf.data();
  }

  int pos = 0;
  while (pos < n)
  {
    int copy = min(n - pos, FRAME_SAMPLES - m_tx_stored);
    memcpy(m_tx_buf.data() + m_tx_stored, in + pos, sizeof(int16_t) * copy);
    m_tx_stored += copy;
    pos         += copy;

    if (m_tx_stored == FRAME_SAMPLES)
    {
      UsrpAudioMsg amsg;
      amsg.setType(USRP_TYPE_VOICE);
      amsg.setKeyup(true);
      amsg.setTg(selectedTg());
      amsg.setAudioData(m_tx_buf.data());

      if (m_udp_seq++ > 0x7fff) m_udp_seq = 0;
      amsg.setSeq(m_udp_seq);

      ostringstream ss;
      if (amsg.pack(ss))
      {
        log(LOGDEBUG, "[RX] Sending USRP voice frame seq=" + to_string(m_udp_seq)
            + " to " + m_usrp_host + ":" + to_string(m_usrp_tx_port));
        sendUdpRaw(ss);
      }
      m_tx_stored = 0;
    }
  }
} /* UsrpClient::sendUsrpAudio */


void UsrpClient::sendUsrpStop(void)
{
  m_ptt_on    = false;
  m_meta_sent = false;
  m_tx_stored = 0;

  UsrpHeaderMsg hdr;
  if (m_udp_seq++ > 0x7fff) m_udp_seq = 0;
  hdr.setSeq(m_udp_seq);

    // keyup = 0, type = USRP_TYPE_VOICE (all zero defaults) → PTT-off
  ostringstream ss;
  if (hdr.pack(ss))
  {
    sendUdpRaw(ss);
    log(LOGINFO, "[RX] USRP PTT-off frame sent (seq=" + to_string(m_udp_seq) + ")");
  }
} /* UsrpClient::sendUsrpStop */


void UsrpClient::sendUsrpMeta(const std::string& cs)
{
  UsrpTlvMetaMsg meta;
  meta.setTg(selectedTg());
  meta.setRptId(m_rptid);
  meta.setCC(m_cc);
  meta.setTS(m_ts);
  meta.setDmrId(m_dmrid);
  meta.setCallsign(cs.empty() ? callsign() : cs);

  if (m_udp_seq++ > 0x7fff) m_udp_seq = 0;
  meta.setSeq(m_udp_seq);

  ostringstream ss;
  if (meta.pack(ss))
  {
    sendUdpRaw(ss);
  }
} /* UsrpClient::sendUsrpMeta */


void UsrpClient::sendUdpRaw(ostringstream& ss)
{
  if (m_usrp_sock == nullptr) { return; }
  IpAddress addr(m_usrp_host);
  m_usrp_sock->write(addr, m_usrp_tx_port,
                     ss.str().data(), ss.str().size());
} /* UsrpClient::sendUdpRaw */


bool UsrpClient::setupAudioPipeline(const std::string& negotiated_codec)
{
  delete m_s16_dec;
  m_s16_dec = nullptr;
  delete m_enc;
  m_enc = nullptr;

  m_s16_dec = AudioDecoder::create("S16");
  if (m_s16_dec == nullptr)
  {
    cerr << "*** ERROR[" << m_section << "]: Cannot create S16 decoder" << endl;
    return false;
  }

  Async::AudioSource* tx_src = m_s16_dec;

  if (INTERNAL_SAMPLE_RATE == 16000)
  {
    auto* interp = new AudioInterpolator(2, coeff_16_8, coeff_16_8_taps);
    tx_src->registerSink(interp, true);
    tx_src = interp;

    float tx_limiter_thresh = -2.0f;
    cfg().getValue(m_section, "USRP_TX_LIMITER_THRESH", tx_limiter_thresh);
    if (tx_limiter_thresh != 0.0f)
    {
      auto* limiter = new AudioCompressor();
      limiter->setThreshold(tx_limiter_thresh);
      limiter->setRatio(0.1f);
      limiter->setAttack(2);
      limiter->setDecay(20);
      limiter->setOutputGain(1.0f);
      tx_src->registerSink(limiter, true);
      tx_src = limiter;
    }

    auto* clipper = new AudioClipper(1.0f);
    tx_src->registerSink(clipper, true);
    tx_src = clipper;
  }

  m_enc = AudioEncoder::create(negotiated_codec);
  if (m_enc == nullptr)
  {
    log(LOGERROR, "Could not create audio encoder: " + negotiated_codec);
    return false;
  }
  log(LOGINFO, "TX Audio Encoder: " + negotiated_codec);

  m_enc->writeEncodedSamples.connect(
      sigc::mem_fun(*this, &UsrpClient::txEncoderOutput));
  m_enc->flushEncodedSamples.connect(
      sigc::mem_fun(*this, &UsrpClient::txEncoderFlushed));

  tx_src->registerSink(m_enc, false);

  delete m_dec;
  m_dec = nullptr;

  m_dec = AudioDecoder::create(negotiated_codec);
  if (m_dec == nullptr)
  {
    cerr << "*** ERROR[" << m_section
         << "]: Cannot create decoder for codec: " << negotiated_codec << endl;
    return false;
  }
  m_dec->allEncodedSamplesFlushed.connect(
      sigc::mem_fun(*this, &UsrpClient::allEncodedSamplesFlushed));

  Async::AudioSource* rx_src = m_dec;

  if (INTERNAL_SAMPLE_RATE == 16000)
  {
    auto* decim = new AudioDecimator(2, coeff_16_8, coeff_16_8_taps);
    rx_src->registerSink(decim, true);
    rx_src = decim;
  }

  float rx_limiter_thresh = -2.0f;
  cfg().getValue(m_section, "USRP_RX_LIMITER_THRESH", rx_limiter_thresh);
  if (rx_limiter_thresh != 0.0f)
  {
    auto* rx_limiter = new AudioCompressor();
    rx_limiter->setThreshold(rx_limiter_thresh);
    rx_limiter->setRatio(0.1f);
    rx_limiter->setAttack(2);
    rx_limiter->setDecay(20);
    rx_limiter->setOutputGain(1.0f);
    rx_src->registerSink(rx_limiter, true);
    rx_src = rx_limiter;
  }

  auto* rx_clipper = new AudioClipper(1.0f);
  rx_src->registerSink(rx_clipper, true);
  rx_src = rx_clipper;

  AudioEncoder* s16_enc = AudioEncoder::create("S16");
  if (s16_enc == nullptr)
  {
    cerr << "*** ERROR[" << m_section << "]: Cannot create S16 encoder" << endl;
    return false;
  }
  s16_enc->writeEncodedSamples.connect(
      sigc::mem_fun(*this, &UsrpClient::sendUsrpAudio));
  s16_enc->flushEncodedSamples.connect(
      sigc::mem_fun(*this, &UsrpClient::sendUsrpStop));

  rx_src->registerSink(s16_enc, true);

  log(LOGINFO, m_section + ": Audio pipeline ready"
      " codec=" + negotiated_codec
      + " INTERNAL_SAMPLE_RATE=" + to_string(INTERNAL_SAMPLE_RATE)
      + (INTERNAL_SAMPLE_RATE == 16000
           ? " (interpolator 8→16k on TX, decimator 16→8k on RX)"
           : " (no resampling needed)"));
  return true;
} /* UsrpClient::setupAudioPipeline */


void UsrpClient::log(int level, const std::string& msg) const
{
  if (m_debug >= level)
  {
    cout << msg << endl;
  }
} /* UsrpClient::log */


/*
 * This file has not been truncated
 */
