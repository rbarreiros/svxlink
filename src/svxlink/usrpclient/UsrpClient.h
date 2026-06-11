/**
@file    UsrpClient.h
@brief   Standalone USRP ↔ SvxReflector bridge
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

#ifndef USRP_CLIENT_H
#define USRP_CLIENT_H


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <sys/time.h>
#include <cstdint>
#include <string>
#include <array>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncConfig.h>
#include <AsyncTimer.h>
#include <AsyncUdpSocket.h>
#include <AsyncAudioDecoder.h>
#include <AsyncAudioEncoder.h>
#include <AsyncAudioFifo.h>
#include <AsyncAudioInterpolator.h>
#include <AsyncAudioDecimator.h>
#include <AsyncAudioAmp.h>
#include <AsyncAudioFilter.h>
#include <AsyncAudioClipper.h>
#include <AsyncAudioCompressor.h>
#include <AsyncAudioPassthrough.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "../reflectorclient/ReflectorClient.h"
#include "../svxlink/contrib/UsrpLogic/UsrpMsg.h"


/****************************************************************************
 *
 * Forward declarations
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Class definitions
 *
 ****************************************************************************/

/**
@brief  Standalone bridge between an SvxReflector and a USRP endpoint

Connects to a SvxReflector using ReflectorClient (TLS, PKI, UDP encryption)
and bridges audio to/from a USRP-protocol UDP endpoint (Analog Bridge,
AllStar, MMDVM_Bridge, etc.).

Audio flow:
  Reflector  →  (OPUS/GSM decode)  →  [S16 resample]  →  USRP UDP TX
  USRP UDP RX  →  [S16 resample]  →  (OPUS/GSM encode)  →  Reflector

Configuration keys (in addition to all ReflectorClient keys):
  USRP_HOST        – USRP peer hostname/IP (required)
  USRP_TX_PORT     – UDP port to send audio to the USRP peer (default 41234)
  USRP_RX_PORT     – UDP port to receive audio from the USRP peer (default 41233)
  DEFAULT_TG       – Single TG selected for outgoing (USRP→reflector) audio
  MONITOR_TGS      – Space/comma-separated TG list the reflector should forward
                     when others transmit (incoming audio); not used for TX
  DMRID            – DMR ID sent in metadata frames (default 0)
  RPTID            – Repeater ID sent in metadata frames (default 0)
  DEFAULT_TS       – Default DMR time-slot (default 1)
  DEFAULT_CC       – Default DMR colour-code (default 1)
  PREAMP           – Local audio gain before encoding, dB (default 0)
  NET_PREAMP       – Network audio gain after decoding, dB (default 0)
  FILTER_TO_USRP   – Biquad filter spec applied to audio going toward USRP
  FILTER_FROM_USRP – Biquad filter spec applied to audio coming from USRP
  NET_LIMITER_THRESH   – Audio compressor threshold for rx path (dBFS, default -1)
  LOCAL_LIMITER_THRESH – Audio compressor threshold for tx path (dBFS, default -1)
  JITTER_BUFFER_DELAY  – Jitter buffer size in ms on the rx path (default 0)
  DEBUG            – Verbosity 0=errors 1=warn 2=info 3=debug (default 0)
*/
class UsrpClient : public ReflectorClient
{
  public:
    UsrpClient(void);
    virtual ~UsrpClient(void) override;

    /**
     * @brief   Initialize the USRP client
     * @param   cfg     Configuration object
     * @param   section Config section name
     * @return  true on success
     */
    bool initialize(Async::Config& cfg, const std::string& section);

    /**
     * @brief   Override the debug verbosity set by the config file
     * @param   level  0=errors 1=warn 2=info 3=debug
     *
     */
    void setDebugLevel(int level) { m_debug = level; }

  protected:

    void onConnected(void) override;
    void onDisconnected(void) override;
    void onLoggedIn(void) override;

    void onCodecNegotiated(const std::string& codec) override;

    void onAudioReceived(uint32_t tg, const std::string& codec,
                         const void* data, int len) override;
    void onAudioFlushed(uint32_t tg) override;
    void onAllSamplesFlushed(void) override;

    void onTalkerStart(uint32_t tg, const std::string& callsign) override;
    void onTalkerStop(uint32_t tg, const std::string& callsign) override;

    Json::Value buildNodeInfo(void) const override;

  private:
    std::string                 m_section;

    std::string                 m_usrp_host;
    uint16_t                    m_usrp_tx_port  = 41234;
    uint16_t                    m_usrp_rx_port  = 41233;
    Async::UdpSocket*           m_usrp_sock     = nullptr;
    int                         m_udp_seq       = 0;

    std::string                 m_callsign;
    uint32_t                    m_dmrid         = 0;
    uint32_t                    m_rptid         = 0;
    uint8_t                     m_cc            = 1;
    uint8_t                     m_ts            = 1;
    uint32_t                    m_default_tg    = 0;
    uint32_t                    m_monitor_tgs   = 0;  

    Async::AudioDecoder*        m_dec           = nullptr;
    Async::AudioEncoder*        m_enc           = nullptr;
    Async::AudioDecoder*        m_s16_dec       = nullptr;
    Async::AudioPassthrough*    m_audio_out     = nullptr;
    Async::AudioPassthrough*    m_audio_in      = nullptr;

    static constexpr int        FRAME_SAMPLES   = USRP_AUDIO_FRAME_LEN;
    std::array<int16_t, FRAME_SAMPLES * 4> m_tx_buf{};
    int                         m_tx_stored     = 0;

    float                       m_tx_preamp     = 1.0f;
    float                       m_rx_preamp     = 1.0f;
    bool                        m_usrp_audio_le = true;

    bool                        m_ptt_on        = false;
    bool                        m_usrp_ptt_on   = false;
    bool                        m_meta_sent     = false;
    struct timeval              m_last_audio_ts{};

    Async::Timer                m_flush_timer;
    Async::Timer                m_tx_watchdog;
    int                         m_debug         = 0;

    UsrpClient(const UsrpClient&)            = delete;
    UsrpClient& operator=(const UsrpClient&) = delete;

    void usrpDatagramReceived(const Async::IpAddress& addr, uint16_t port,
                              void* buf, int count);

    void sendUsrpAudio(const void* pcm16le, int count);
    void sendUsrpStop(void);
    void sendUsrpMeta(const std::string& callsign = "");

    void sendUdpRaw(std::ostringstream& ss);
    void handleVoiceFrame(const void* buf, int count);
    void handleStreamStop(void);

    void flushTimeout(Async::Timer* t = nullptr);
    void allEncodedSamplesFlushed(void);

    void txWatchdogExpired(Async::Timer* t = nullptr);

    void txEncoderOutput(const void* buf, int count);
    void txEncoderFlushed(void);

    bool setupAudioPipeline(const std::string& codec);

    void log(int level, const std::string& msg) const;

}; /* class UsrpClient */


#endif /* USRP_CLIENT_H */


/*
 * This file has not been truncated
 */
