/**
@file    ReflectorClient.h
@brief   A standalone base class for connecting to the SvxReflector
@author  Rui Barreiros based on ReflectorLogic by Tobias Blomberg / SM0SVX

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

#ifndef REFLECTOR_CLIENT_BASE_H
#define REFLECTOR_CLIENT_BASE_H


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <sys/time.h>
#include <string>
#include <set>
#include <vector>
#include <cstdint>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncConfig.h>
#include <AsyncTcpPrioClient.h>
#include <AsyncFramedTcpConnection.h>
#include <AsyncTimer.h>
#include <AsyncSslContext.h>
#include <AsyncSslKeypair.h>
#include <AsyncSslCertSigningReq.h>
#include <AsyncSslX509.h>
#include <AsyncEncryptedUdpSocket.h>
#include <json/json.h>
#include <sigc++/sigc++.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "../reflector/ReflectorMsg.h"


/****************************************************************************
 *
 * Forward declarations
 *
 ****************************************************************************/

namespace Async
{
  class EncryptedUdpSocket;
  class IpAddress;
};
class ReflectorMsg;
class ReflectorUdpMsg;


/****************************************************************************
 *
 * Class definitions
 *
 ****************************************************************************/

/**
@brief  Standalone base class for connecting to an SvxReflector

Handles the full client-side protocol: TLS connection, PKI/certificate
management, UDP encryption, heartbeats, and talk group management.

Subclasses override the virtual callback methods to react to events and
audio without being coupled to the svxlink LogicBase plugin system.

Configuration keys (same as ReflectorLogic):
  CALLSIGN           – required, node callsign
  AUTH_KEY           – HMAC auth key (legacy, pre-TLS)
  HOSTS              – one or more host[:port] entries
  DNS_DOMAIN         – SRV DNS domain (alternative to HOSTS)
  HOST_PORT          – default port if not specified per host (default 5300)
  CERT_PKI_DIR       – directory for keys/certs (default var/lib/svxlink/pki)
  CERT_KEYFILE       – path to private key file
  CERT_CRTFILE       – path to certificate file
  CERT_CSRFILE       – path to CSR file
  CERT_CAFILE        – path to CA bundle
  CERT_EMAIL         – e-mail address(es) for the certificate SAN
  CERT_SUBJ_*        – certificate subject fields
  CERT_DOWNLOAD_CA_BUNDLE – whether to download CA bundle from server
  VERBOSE            – log node join/leave events (default true)
  UDP_HEARTBEAT_INTERVAL  – seconds between UDP heartbeats (default 15)
*/
class ReflectorClient : public sigc::trackable
{
  public:
    ReflectorClient(void);
    virtual ~ReflectorClient(void);

    /**
     * @brief   Initialize the client from an Async::Config section
     * @param   cfg     Configuration object
     * @param   section Config section name containing the keys listed above
     * @return  true on success
     */
    bool initialize(Async::Config& cfg, const std::string& section);

    /**
     * @brief   Select (join) a talk group
     * @param   tg  Talk group number, 0 to deselect
     */
    void selectTg(uint32_t tg);

    /**
     * @brief   Set the set of talk groups to monitor
     * @param   tgs  Set of TG numbers; replaces any previous monitor set
     */
    void monitorTgs(const std::set<uint32_t>& tgs);

    /**
     * @brief   Send pre-encoded audio bytes to the reflector
     *
     * Only useful for subclasses that also transmit. The codec used must
     * match the one negotiated at login (see codec()).
     */
    void sendEncodedAudio(const void* buf, int count);

    /**
     * @brief   Signal end of a transmission to the reflector
     */
    void flushEncodedAudio(void);

    bool        isConnected(void) const;
    bool        isLoggedIn(void)  const { return m_con_state == STATE_CONNECTED; }

    const std::string& callsign(void)     const { return m_callsign; }
    const std::string& codec(void)        const { return m_codec; }
    uint32_t           selectedTg(void)   const { return m_selected_tg; }
    uint32_t           currentAudioTg(void) const { return m_current_audio_tg; }

  protected:
    /**
     * @brief   Called when the TCP connection is established (before login)
     */
    virtual void onConnected(void) {}

    /**
     * @brief   Called when the TCP connection is lost
     */
    virtual void onDisconnected(void) {}

    /**
     * @brief   Called when the full login handshake completes and audio
     *          can flow.  Default implementation sends the monitored TG list.
     */
    virtual void onLoggedIn(void);

    /**
     * @brief   Called for each received encoded audio frame
     * @param   tg      Talk group the audio belongs to
     * @param   codec   Codec name ("OPUS", "SPEEX", "GSM")
     * @param   data    Raw codec payload bytes
     * @param   len     Number of bytes
     */
    virtual void onAudioReceived(uint32_t tg, const std::string& codec,
                                 const void* data, int len) {}

    /**
     * @brief   Called when the remote talker ends their transmission
     * @param   tg  Talk group the flush belongs to
     */
    virtual void onAudioFlushed(uint32_t tg) {}

    /**
     * @brief   Called when a node starts talking on a monitored TG
     */
    virtual void onTalkerStart(uint32_t tg, const std::string& callsign) {}

    /**
     * @brief   Called when a node stops talking on a monitored TG
     */
    virtual void onTalkerStop(uint32_t tg, const std::string& callsign) {}

    /**
     * @brief   Called when a node connects to the reflector
     */
    virtual void onNodeJoined(const std::string& callsign) {}

    /**
     * @brief   Called when a node disconnects from the reflector
     */
    virtual void onNodeLeft(const std::string& callsign) {}

    /**
     * @brief   Called when the server requests a QSY to another TG
     */
    virtual void onQsyRequest(uint32_t tg) {}

    /**
     * @brief   Called when all TX samples have been flushed by the server
     */
    virtual void onAllSamplesFlushed(void) {}

    /**
     * @brief   Override to add extra JSON fields to the MsgNodeInfo payload
     *          sent at login.  Return a JSON object; its members are merged.
     */
    virtual Json::Value buildNodeInfo(void) const { return Json::Value(Json::objectValue); }

  private:
    typedef enum
    {
      STATE_DISCONNECTED,
      STATE_EXPECT_CA_INFO,
      STATE_EXPECT_AUTH_CHALLENGE,
      STATE_EXPECT_START_ENCRYPTION,
      STATE_EXPECT_CA_BUNDLE,
      STATE_EXPECT_SSL_CON_READY,
      STATE_EXPECT_AUTH_ANSWER,
      STATE_AUTHENTICATED,
      STATE_EXPECT_SERVER_INFO,
      STATE_EXPECT_START_UDP_ENCRYPTION,
      STATE_EXPECT_UDP_HEARTBEAT,
      STATE_CONNECTED
    } ConState;

    static const ConState STATE_TCP_CONNECTED =
                              STATE_EXPECT_START_UDP_ENCRYPTION;

    typedef Async::TcpPrioClient<Async::FramedTcpConnection> FramedTcpClient;

    static const unsigned DEFAULT_UDP_HEARTBEAT_TX_CNT_RESET = 15;
    static const unsigned UDP_HEARTBEAT_RX_CNT_RESET         = 60;
    static const unsigned TCP_HEARTBEAT_TX_CNT_RESET         = 10;
    static const unsigned TCP_HEARTBEAT_RX_CNT_RESET         = 15;

    std::string                     m_section;
    std::string                     m_callsign;
    std::string                     m_auth_key;
    std::string                     m_codec;
    std::string                     m_pki_dir;
    std::string                     m_cafile;
    std::string                     m_crtfile;
    std::string                     m_keyfile;
    std::string                     m_csrfile;
    bool                            m_download_ca_bundle  = true;
    bool                            m_verbose             = true;

    FramedTcpClient                 m_con;
    unsigned                        m_msg_type            = 0;
    Async::EncryptedUdpSocket*      m_udp_sock            = nullptr;
    ReflectorUdpMsg::ClientId       m_client_id           = 0;
    ConState                        m_con_state           = STATE_DISCONNECTED;
    Async::Timer                    m_reconnect_timer;
    Async::Timer                    m_heartbeat_timer;
    Async::Timer                    m_flush_timeout_timer;
    UdpCipher::IVCntr               m_next_udp_rx_seq     = 0;
    UdpCipher::IVCntr               m_udp_cipher_iv_cntr  = 1;
    UdpCipher::AAD                  m_aad;
    std::vector<uint8_t>            m_udp_cipher_iv_rand;
    unsigned                        m_udp_heartbeat_tx_cnt_reset = DEFAULT_UDP_HEARTBEAT_TX_CNT_RESET;
    unsigned                        m_udp_heartbeat_tx_cnt = 0;
    unsigned                        m_udp_heartbeat_rx_cnt = 0;
    unsigned                        m_tcp_heartbeat_tx_cnt = 0;
    unsigned                        m_tcp_heartbeat_rx_cnt = 0;
    struct timeval                  m_last_talker_timestamp;
    uint32_t                        m_selected_tg         = 0;
    uint32_t                        m_current_audio_tg    = 0;
    std::set<uint32_t>              m_monitor_tgs;

    Async::SslContext               m_ssl_ctx;
    Async::SslKeypair               m_ssl_pkey;
    Async::SslCertSigningReq        m_ssl_csr;
    Async::SslX509                  m_ssl_cert;

    ReflectorClient(const ReflectorClient&) = delete;
    ReflectorClient& operator=(const ReflectorClient&) = delete;

    void onTcpConnected(void);
    void onTcpDisconnected(Async::TcpConnection* con,
                           Async::TcpConnection::DisconnectReason reason);
    bool onVerifyPeer(Async::TcpConnection* con, bool preverify_ok,
                      X509_STORE_CTX* x509_store_ctx);
    void onSslConnectionReady(Async::TcpConnection* con);
    void onFrameReceived(Async::FramedTcpConnection* con,
                         std::vector<uint8_t>& data);

    void handleMsgError(std::istream& is);
    void handleMsgProtoVerDowngrade(std::istream& is);
    void handleMsgCAInfo(std::istream& is);
    void handleMsgCABundle(std::istream& is);
    void handleMsgStartEncryption(void);
    void handleMsgAuthChallenge(std::istream& is);
    void handleMsgAuthOk(void);
    void handleMsgClientCsrRequest(void);
    void handleMsgClientCert(std::istream& is);
    void handleMsgServerInfo(std::istream& is);
    void handleMsgNodeList(std::istream& is);
    void handleMsgNodeJoined(std::istream& is);
    void handleMsgNodeLeft(std::istream& is);
    void handleMsgTalkerStart(std::istream& is);
    void handleMsgTalkerStop(std::istream& is);
    void handleMsgRequestQsy(std::istream& is);
    void handleMsgStartUdpEncryption(std::istream& is);

    bool udpCipherDataReceived(const Async::IpAddress& addr, uint16_t port,
                               void* buf, int count);
    void udpDatagramReceived(const Async::IpAddress& addr, uint16_t port,
                             void* aad, void* buf, int count);

    void sendMsg(const ReflectorMsg& msg);
    void sendUdpMsg(const UdpCipher::AAD& aad, const ReflectorUdpMsg& msg);
    void sendUdpMsg(const ReflectorUdpMsg& msg);
    void sendUdpRegisterMsg(void);

    void connect(void);
    void disconnect(void);
    void reconnect(void);

    void handleTimerTick(Async::Timer* t);
    void flushTimeout(Async::Timer* t = nullptr);

    bool setupPki(Async::Config& cfg);
    bool loadClientCertificate(void);

}; /* class ReflectorClient */


#endif /* REFLECTOR_CLIENT_BASE_H */


/*
 * This file has not been truncated
 */
