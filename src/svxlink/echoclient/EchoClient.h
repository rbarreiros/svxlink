/**
@file    EchoClient.h
@brief   Standalone EchoLink ↔ SvxReflector bridge
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

#ifndef ECHO_CLIENT_H
#define ECHO_CLIENT_H


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <sys/types.h>
#include <regex.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncConfig.h>
#include <AsyncTimer.h>
#include <AsyncIpAddress.h>
#include <AsyncAudioDecoder.h>
#include <AsyncAudioEncoder.h>
#include <AsyncAudioSplitter.h>
#include <AsyncAudioSelector.h>
#include <EchoLinkQso.h>
#include <EchoLinkDirectory.h>
#include <EchoLinkStationData.h>
#include <EchoLinkProxy.h>


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
@brief  Standalone bridge between an SvxReflector and the EchoLink network

Connects to a SvxReflector using ReflectorClient (TLS, PKI, UDP encryption)
and bridges audio to/from EchoLink stations via the EchoLink library.

Audio flow:
  Reflector → (decode) → float PCM → AudioSplitter → EchoLink Qso (GSM encode → UDP)
  EchoLink Qso (UDP → GSM decode → float PCM) → AudioSelector → (encode) → Reflector

Configuration keys (in addition to all ReflectorClient keys):

  EchoLink registration:
    EL_CALLSIGN      – EchoLink callsign (required, e.g. MYCALL-L)
    EL_PASSWORD      – EchoLink directory password (required)
    EL_SERVERS       – Directory server(s), space-separated (default: servers.echolink.org)
    EL_LOCATION      – Station location string shown in directory (required)
    EL_SYSOPNAME     – Operator name (required)
    EL_DESCRIPTION   – Info text sent to connecting stations (required)

  Connection control:
    MAX_CONNECTIONS  – Maximum simultaneous EchoLink connections (default: 1)
    MAX_QSOS         – Maximum simultaneous active QSOs (default: 1)
    DROP_ALL_INCOMING – Drop all incoming connections (default: 0)
    DROP_INCOMING    – Regex of callsigns to silently drop (default: ^$)
    REJECT_INCOMING  – Regex of callsigns to reject with message (default: ^$)
    ACCEPT_INCOMING  – Regex of callsigns to accept (default: ^.*$)
    REJECT_OUTGOING  – Regex of callsigns to refuse outgoing (default: ^$)
    ACCEPT_OUTGOING  – Regex of callsigns allowed outgoing (default: ^.*$)
    REJECT_CONF      – Reject incoming conference nodes (default: 0)
    ALLOW_IP         – IP subnet that bypasses directory validation

  Auto-connect:
    AUTOCON_ECHOLINK_ID – EchoLink node ID to auto-connect to (default: 0=off)
    AUTOCON_TIME        – Interval in seconds to retry auto-connect (default: 180)

  Proxy:
    EL_PROXY_SERVER  – EchoLink proxy server hostname
    EL_PROXY_PORT    – EchoLink proxy server port (default: 8100)
    EL_PROXY_PASSWORD – Proxy password

  Talk group:
    DEFAULT_TG       – Reflector talk group to select after login (default: 0)
    MONITOR_TGS      – Space/comma-separated TG list to monitor

  Misc:
    BIND_ADDR        – Local IP address to bind EchoLink UDP sockets
    DEBUG            – If >= 3, print extra debug.  Errors, warnings, and
                       operational INFO (including EchoLink directory status)
                       are always printed (default: 0)
*/
class EchoClient : public ReflectorClient
{
  public:
    EchoClient(void);
    virtual ~EchoClient(void) override;

    /**
     * @brief   Initialize the EchoLink client
     * @param   cfg     Configuration object
     * @param   section Config section name
     * @return  true on success
     */
    bool initialize(Async::Config& cfg, const std::string& section);

  protected:
    // -- ReflectorClient overrides -------------------------------------------

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
    std::string                     m_section;

    // -- EchoLink directory ---------------------------------------------------
    EchoLink::Directory*            m_dir               = nullptr;
    Async::Timer*                   m_dir_refresh_timer = nullptr;
    EchoLink::Proxy*                m_proxy             = nullptr;

    // -- EchoLink station identity --------------------------------------------
    std::string                     m_el_callsign;
    std::string                     m_el_password;
    std::string                     m_location;
    std::string                     m_sysop_name;
    std::string                     m_description;

    // -- Active QSOs ----------------------------------------------------------
    std::vector<EchoLink::Qso*>     m_qsos;
    EchoLink::Qso*                  m_talker            = nullptr;
    unsigned                        m_max_connections   = 1;
    unsigned                        m_max_qsos          = 1;

    // -- Incoming connection filtering ----------------------------------------
    std::string                     m_allow_ip;
    bool                            m_drop_all_incoming = false;
    regex_t*                        m_drop_incoming_regex    = nullptr;
    regex_t*                        m_reject_incoming_regex  = nullptr;
    regex_t*                        m_accept_incoming_regex  = nullptr;
    regex_t*                        m_reject_outgoing_regex  = nullptr;
    regex_t*                        m_accept_outgoing_regex  = nullptr;
    bool                            m_reject_conf       = false;

    // -- Connection rate limiting ---------------------------------------------
    unsigned                        m_num_con_max       = 0;
    time_t                          m_num_con_ttl       = 5 * 60;
    time_t                          m_num_con_block_time = 120 * 60;
    struct NumConStn
    {
      unsigned        num_con;
      struct timeval  last_con;
      NumConStn(unsigned n, struct timeval t) : num_con(n), last_con(t) {}
    };
    std::map<std::string, NumConStn> m_num_con_map;
    Async::Timer*                   m_num_con_update_timer = nullptr;

    // -- Auto-connect ---------------------------------------------------------
    int                             m_autocon_echolink_id = 0;
    int                             m_autocon_time      = 3 * 60 * 1000;
    Async::Timer*                   m_autocon_timer     = nullptr;

    // -- Pending connect-by-ID ------------------------------------------------
    int                             m_pending_connect_id = -1;
    EchoLink::StationData           m_last_disc_station;

    // -- Talk group -----------------------------------------------------------
    uint32_t                        m_default_tg        = 0;

    // -- State ----------------------------------------------------------------
    bool                            m_reflector_is_rx   = false;

    /**
     * Last EchoLink INFO (NDATA) text per QSO — many clients (e.g. EchoLink for
     * Android) resend the same station block on a timer; we only log when it
     * changes so DEBUG stays readable.
     */
    std::unordered_map<const EchoLink::Qso*, std::string> m_last_qso_info_msg;

    // -- Audio pipeline -------------------------------------------------------
    // RX: Reflector encoded → m_dec (float PCM) → m_splitter → each Qso (GSM encode → UDP)
    // TX: Qso (UDP → GSM decode → float PCM) → m_selector → m_enc → Reflector
    Async::AudioDecoder*            m_dec               = nullptr;
    Async::AudioEncoder*            m_enc               = nullptr;
    Async::AudioSplitter*           m_splitter          = nullptr;
    Async::AudioSelector*           m_selector          = nullptr;

    // -- Logging --------------------------------------------------------------
    int                             m_debug             = 0;

    EchoClient(const EchoClient&)            = delete;
    EchoClient& operator=(const EchoClient&) = delete;

    // -- EchoLink directory callbacks -----------------------------------------
    void onELStatusChanged(EchoLink::StationData::Status status);
    void onELStationListUpdated(void);
    void onELError(const std::string& msg);

    // -- Incoming connection --------------------------------------------------
    void onIncomingConnection(const Async::IpAddress& ip,
                              const std::string& callsign,
                              const std::string& name,
                              const std::string& priv);

    // -- Per-QSO callbacks (sigc::bind passes the Qso* as extra arg) ----------
    void onQsoStateChange(EchoLink::Qso::State state, EchoLink::Qso* qso);
    void onQsoIsReceiving(bool is_receiving, EchoLink::Qso* qso);
    void onQsoAudioReceivedRaw(EchoLink::Qso::RawPacket* packet,
                               EchoLink::Qso* qso);
    void onQsoChatMsgReceived(const std::string& msg, EchoLink::Qso* qso);
    void onQsoInfoMsgReceived(const std::string& msg, EchoLink::Qso* qso);

    // -- Directory helpers ----------------------------------------------------
    void getDirectoryList(Async::Timer* t = nullptr);

    // -- QSO management -------------------------------------------------------
    EchoLink::Qso* createQso(const Async::IpAddress& ip,
                             const std::string& remote_callsign,
                             const std::string& remote_name  = "",
                             const std::string& remote_priv  = "");
    void createOutgoingConnection(const EchoLink::StationData& station);
    void connectByNodeId(int node_id);
    void destroyQso(EchoLink::Qso* qso);
    EchoLink::Qso* findFirstTalker(void) const;
    /** Remote EchoLink callsigns currently keyed (PTT), comma-separated. */
    std::string activeEchoLinkCallsigns(void) const;
    int  numConnectedStations(void) const;
    void broadcastTalkerStatus(void);
    void updateDescription(void);

    // -- Audio pipeline -------------------------------------------------------
    bool setupAudioPipeline(const std::string& codec);
    void teardownAudioPipeline(void);

    // -- Access control -------------------------------------------------------
    bool setupAccessControl(Async::Config& cfg, const std::string& section);
    bool setRegex(regex_t*& regex, const std::string& regex_str,
                  const std::string& tag);
    bool numConCheck(const std::string& callsign);
    void numConUpdate(Async::Timer* t = nullptr);

    // -- Auto-connect ---------------------------------------------------------
    void checkAutoCon(Async::Timer* t = nullptr);

    // -- Helpers --------------------------------------------------------------
    void cleanup(void);
    void log(int level, const std::string& msg) const;

}; /* class EchoClient */


#endif /* ECHO_CLIENT_H */


/*
 * This file has not been truncated
 */
