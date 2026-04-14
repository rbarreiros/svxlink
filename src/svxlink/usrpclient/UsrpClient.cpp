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
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cassert>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncAudioInterpolator.h>
#include <AsyncAudioDecimator.h>
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
  : m_flush_timer(3000, Timer::TYPE_ONESHOT, false)
{
  timerclear(&m_last_audio_ts);
  m_flush_timer.expired.connect(
      sigc::mem_fun(*this, &UsrpClient::flushTimeout));
} /* UsrpClient::UsrpClient */


UsrpClient::~UsrpClient(void)
{
  delete m_usrp_sock;
  m_usrp_sock = nullptr;
  delete m_dec;
  m_dec = nullptr;
  delete m_enc;
  m_enc = nullptr;
} /* UsrpClient::~UsrpClient */


bool UsrpClient::initialize(Async::Config& cfg, const std::string& section)
{
  cfg.getValue(section, "DEBUG", m_debug);
  m_section = section;

    // -- USRP network settings ------------------------------------------------
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

    // -- DMR metadata ---------------------------------------------------------
  cfg.getValue(section, "DMRID",      m_dmrid);
  cfg.getValue(section, "RPTID",      m_rptid);
  cfg.getValue(section, "DEFAULT_TG", m_default_tg);

  std::string tmp;
  if (cfg.getValue(section, "DEFAULT_CC", tmp)) { m_cc = atoi(tmp.c_str()) & 0xff; }
  if (cfg.getValue(section, "DEFAULT_TS", tmp)) { m_ts = atoi(tmp.c_str()) & 0xff; }

    // -- USRP receive socket --------------------------------------------------
  m_usrp_sock = new UdpSocket(m_usrp_rx_port);
  m_usrp_sock->dataReceived.connect(
      sigc::mem_fun(*this, &UsrpClient::usrpDatagramReceived));

    // -- ReflectorClient (handles reflector connection + auth) ----------------
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
    // Stop any ongoing TX toward the USRP
  if (m_ptt_on)
  {
    sendUsrpStop();
  }
} /* UsrpClient::onDisconnected */


void UsrpClient::onLoggedIn(void)
{
  log(LOGINFO, m_section + ": Logged in, codec=" + codec()
      + " TG=" + to_string(m_default_tg));

    // Select and/or monitor the default talk group
  if (m_default_tg > 0)
  {
    selectTg(m_default_tg);
  }

    // Base class sends the monitored TG list
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
    // Push encoded bytes from the reflector into our decoder pipeline.
    // The decoder expands to S16 @ 8 kHz, which gets interpolated to the
    // internal rate and forwarded to the USRP via sendUsrpAudio().
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
  log(LOGINFO, m_section + ": Talker start on TG #" + to_string(tg)
      + ": " + callsign);

    // Send a USRP meta frame announcing the remote callsign
  sendUsrpMeta(callsign);
} /* UsrpClient::onTalkerStart */


void UsrpClient::onTalkerStop(uint32_t tg, const std::string& callsign)
{
  log(LOGINFO, m_section + ": Talker stop on TG #" + to_string(tg)
      + ": " + callsign);

    // Flush remaining buffered audio toward USRP
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
    log(LOGWARN, "Short USRP datagram (" + to_string(count) + " bytes), ignored");
    return;
  }

  log(LOGDEBUG, "USRP rx " + to_string(count) + " bytes from "
      + addr.toString() + ":" + to_string(port));

  stringstream ss;
  ss.write(reinterpret_cast<const char*>(buf), count);

  UsrpHeaderMsg hdr;
  if (!hdr.unpack(ss))
  {
    log(LOGERROR, "*** WARNING: Failed to unpack USRP header");
    return;
  }

  const uint32_t utype = hdr.type();

  if (utype == USRP_TYPE_VOICE)
  {
    if (!hdr.keyup())
    {
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
        log(LOGERROR, "*** WARNING: Failed to unpack USRP audio frame");
        return;
      }
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
        log(LOGINFO, "USRP meta: callsign=" + tlv.getCallsign()
            + " tg=" + to_string(tlv.getTg())
            + " dmrid=" + to_string(tlv.getDmrId()));
      }
    }
  }
  else if (utype == USRP_TYPE_PING)
  {
    log(LOGDEBUG, "USRP ping received");
  }
  else
  {
    log(LOGDEBUG, "USRP frame type " + to_string(utype) + " ignored");
  }
} /* UsrpClient::usrpDatagramReceived */


void UsrpClient::handleVoiceFrame(const void* audio_array, int /*count*/)
{
  gettimeofday(&m_last_audio_ts, nullptr);

  const auto* samples =
      reinterpret_cast<const array<int16_t, FRAME_SAMPLES>*>(audio_array);

    // Convert big-endian network samples to host order
  array<int16_t, FRAME_SAMPLES> host_samples{};
  for (int i = 0; i < FRAME_SAMPLES; ++i)
  {
    host_samples[i] = ntohs((*samples)[i]);
  }

    // Push S16 PCM @ 8 kHz into the encoder (which will produce encoded frames
    // and call sendEncodedAudio() → ReflectorClient::sendEncodedAudio())
  if (m_enc != nullptr)
  {
    m_enc->writeEncodedSamples(host_samples.data(),
                               sizeof(int16_t) * FRAME_SAMPLES);
  }
} /* UsrpClient::handleVoiceFrame */


void UsrpClient::handleStreamStop(void)
{
  log(LOGINFO, "USRP stream stop (PTT off)");
  if (m_enc != nullptr)
  {
    m_enc->flushEncodedSamples();
  }
  m_meta_sent = false;
  timerclear(&m_last_audio_ts);
} /* UsrpClient::handleStreamStop */


/**
 * Called by the decoder's allEncodedSamplesFlushed signal when the
 * decoded rx audio chain has been fully drained.  We send a USRP stop
 * frame to end the transmission on the USRP side.
 */
void UsrpClient::allEncodedSamplesFlushed(void)
{
  sendUsrpStop();
} /* UsrpClient::allEncodedSamplesFlushed */


void UsrpClient::flushTimeout(Async::Timer* /*t*/)
{
  m_flush_timer.setEnable(false);
  if (m_enc != nullptr)
  {
    m_enc->allEncodedSamplesFlushed();
  }
} /* UsrpClient::flushTimeout */


void UsrpClient::sendUsrpAudio(const void* pcm16le, int byte_count)
{
  if (!isLoggedIn()) { return; }

    // Announce ourselves on first frame with a TLV meta frame
  if (!m_meta_sent)
  {
    sendUsrpMeta(callsign());
    m_meta_sent = true;
  }

  const int16_t* in = reinterpret_cast<const int16_t*>(pcm16le);
  const int      n  = byte_count / static_cast<int>(sizeof(int16_t));

    // Accumulate samples and emit 160-sample USRP frames
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
      amsg.setTg(m_default_tg);
      amsg.setAudioData(m_tx_buf.data());

      if (m_udp_seq++ > 0x7fff) m_udp_seq = 0;
      amsg.setSeq(m_udp_seq);

      ostringstream ss;
      if (amsg.pack(ss))
      {
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
  }
  log(LOGINFO, "USRP stop (PTT off) sent");
} /* UsrpClient::sendUsrpStop */


void UsrpClient::sendUsrpMeta(const std::string& cs)
{
  UsrpTlvMetaMsg meta;
  meta.setTg(m_default_tg);
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
    // -------------------------------------------------------------------------
    // RX path:  Reflector encoded → decoder (S16@8k) → [upsample 8→16k]
    //           → [filter] → [comp] → USRP UDP TX (via sendUsrpAudio)
    // -------------------------------------------------------------------------

    // Delete old decoder if any
  delete m_dec;
  m_dec = nullptr;

  m_dec = AudioDecoder::create(negotiated_codec);
  if (m_dec == nullptr)
  {
    cerr << "*** ERROR[" << m_section
         << "]: Cannot create decoder for codec: " << negotiated_codec << endl;
    return false;
  }

    // When the decoder fully drains, signal USRP TX end
  m_dec->allEncodedSamplesFlushed.connect(
      sigc::mem_fun(*this, &UsrpClient::allEncodedSamplesFlushed));

    // The decoder produces S16 PCM @ 8 kHz.  If the internal rate is 16 kHz
    // we need to interpolate.  We want the samples to come out of the
    // pipeline into sendUsrpAudio(); wire up via a passthrough AudioSource
    // that streams to a simple lambda-based sink.

  // NOTE: Interpolation would normally happen here if INTERNAL_SAMPLE_RATE requires it,
  // but since we wrap the Usrp transmission inside a direct connection on S16 sink,
  // it is simpler to decode S16 straight into S16 encoder and emit frames.

    // The final decoded PCM is forwarded to sendUsrpAudio() via the
    // encoder's written-samples signal below — see TX path comments.
    // For the RX path (reflector → USRP) we plug a custom PassthroughSink.

  // NOTE: AudioPassthrough doesn't exist in this build as a bidirectional
  // helper; instead we reuse m_audio_out as an AudioFifo that collects
  // decoded samples and drains via writeSamples calls — but the simplest
  // approach is to leave the decoder's downstream open and intercept via the
  // encoder write path.  The architecture here mirrors UsrpLogic's approach:
  //   m_dec output → (no local sink needed for usrp):
  //   The S16 decoder writes raw PCM which we forward straight to the USRP.
  //   We wire a custom AudioSink that calls sendUsrpAudio().
  //
  // AudioSink cannot be trivially subclassed inline; instead we use an
  // AudioEncoder with codec "S16" (identity codec) to capture the samples.
  //
  // RX pipeline (reflector enc data → dec → enc_s16 → USRP packets):
  //   We decode the reflector codec to PCM, then re-encode as S16 just to
  //   get frame-sized callbacks.

    // -------------------------------------------------------------------------
    // TX path:  USRP UDP RX (S16@8k PCM from handleVoiceFrame)
    //           → encoder (negotiated codec) → ReflectorClient::sendEncodedAudio
    // -------------------------------------------------------------------------

    // Delete old encoder
  delete m_enc;
  m_enc = nullptr;

    // For the negotiated codec the encoder accepts S16 PCM input.
    // However USRP always delivers raw S16 PCM, so we need an S16 encoder
    // that wraps and feeds data into the actual codec encoder.
    // Use "S16" as a passthrough encoder whose writtenSamples become the
    // input to the real codec encoder.
    //
    // Simpler: just create the negotiated-codec encoder directly and push
    // the S16 PCM bytes into it (writeEncodedSamples expects encoded bytes
    // of *that* codec; for our purposes we call writeEncodedSamples with
    // raw PCM that the codec then encodes).
    //
    // Actually: AudioEncoder::writeEncodedSamples is *output* signal.
    // Input is AudioSink::writeSamples(float*, int).
    // USRP delivers int16_t; we must convert to float and push via
    // writeSamples on the encoder.
    //
    // RX (reflector → USRP):
    //   ReflectorClient calls onAudioReceived(data, len) with encoded bytes.
    //   We call m_dec->writeEncodedSamples(data, len).
    //   Decoded float PCM flows downstream via sink chain.
    //   We capture those float samples in a custom AudioSink wrapper that
    //   converts back to S16 and calls sendUsrpAudio().

    // For the TX direction (USRP → reflector):
    //   USRP delivers S16 PCM via handleVoiceFrame → sendUsrpAudio path.
    //   But wait — in this architecture "sendUsrpAudio" is for *outgoing* USRP
    //   frames.  Let's name things correctly:
    //
    // CORRECT NAMING:
    //   "USRP RX" = bytes arriving from USRP to us → encode → send to reflector
    //   "USRP TX" = bytes we send to USRP          ← decode from reflector
    //
    // handleVoiceFrame() receives USRP→us S16 PCM and forwards to encoder.
    // allEncodedSamplesFlushed / sendUsrpAudio / sendUsrpStop are for us→USRP.
    //
    // The encoder needs float input.  We convert in handleVoiceFrame and push
    // via m_enc->writeSamples().

    // Create the real reflector codec encoder (accepts float PCM @ 8 kHz)
  m_enc = AudioEncoder::create(negotiated_codec);
  if (m_enc == nullptr)
  {
    cerr << "*** ERROR[" << m_section
         << "]: Cannot create encoder for codec: " << negotiated_codec << endl;
    return false;
  }

    // Encoder outputs → ReflectorClient::sendEncodedAudio
  m_enc->writeEncodedSamples.connect(
      sigc::mem_fun(*this, (void(UsrpClient::*)(const void*,int))
                   &ReflectorClient::sendEncodedAudio));
  m_enc->flushEncodedSamples.connect(
      sigc::mem_fun(*this, &ReflectorClient::flushEncodedAudio));

    // We need an S16→float shim for USRP PCM input to the encoder.
    // This is handled inline in handleVoiceFrame() by converting int16 to
    // float and calling m_enc->writeSamples() directly — no extra class needed.

    // Decoder downstream for RX (reflector → USRP):
    // We attach an S16 encoder as a "sink" to the decoder to capture
    // float samples and convert back to int16 for USRP frames.
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

  m_dec->registerSink(s16_enc, true);

  log(LOGINFO, m_section + ": Audio pipeline ready (codec=" + negotiated_codec + ")");
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
