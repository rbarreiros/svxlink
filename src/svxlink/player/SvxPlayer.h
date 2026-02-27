/**
@file    SvxPlayer.h
@brief   Reflector client that plays audio files to a talk group
@author  Rui Barreiros <rbarreiros@gmail.com>
@date    2026-02-27

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

#ifndef SVX_PLAYER_INCLUDED
#define SVX_PLAYER_INCLUDED


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <sys/time.h>
#include <string>
#include <queue>
#include <vector>
#include <cstdint>

#include <sigc++/sigc++.h>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncConfig.h>
#include <AsyncTimer.h>
#include <AsyncTcpPrioClient.h>
#include <AsyncFramedTcpConnection.h>
#include <AsyncAudioEncoder.h>
#include <AsyncAudioDecoder.h>
#include <AsyncAudioPacer.h>
#include <AsyncSslContext.h>
#include <AsyncSslKeypair.h>
#include <AsyncSslCertSigningReq.h>
#include <AsyncSslX509.h>
#include <AsyncPty.h>
#include <AsyncFdWatch.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "../reflector/ReflectorMsg.h"
#include "Scheduler.h"
#include "MsgHandler.h"


/****************************************************************************
 *
 * Forward declarations
 *
 ****************************************************************************/

namespace Async
{
  class EncryptedUdpSocket;
};


/****************************************************************************
 *
 * Class definitions
 *
 ****************************************************************************/

/**
@brief  Reflector client that plays audio files to a TG
@author Rodrigo Barreiros
@date   2026-02-27

SvxPlayer connects to an SvxReflector, authenticates with full PKI/SSL just
like ReflectorLogic, selects a talk group, and transmits audio from WAV/GSM/
raw PCM files.  Playback can be triggered by a cron-like schedule or on demand
through a PTY interface.
*/
class SvxPlayer : public sigc::trackable
{
  public:
    /**
     * @brief  Constructor
     */
    SvxPlayer(void);

    /**
     * @brief  Destructor
     */
    ~SvxPlayer(void);

    /**
     * @brief  Initialize the player from configuration
     * @param  cfg   Previously opened Async::Config object
     * @return true on success
     */
    bool initialize(Async::Config& cfg);

    /**
     * @brief  Queue a file for playback on the given talk group
     * @param  file  Path to the audio file
     * @param  tg    Talk group (0 = use DEFAULT_TG)
     */
    void playFile(const std::string& file, uint32_t tg = 0);

    /**
     * @brief  Abort current playback and clear the playback queue
     */
    void stop(void);

  private:
    struct PlayRequest
    {
      std::string file;
      uint32_t    tg;
    };

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

    static const unsigned DEFAULT_UDP_HEARTBEAT_TX_CNT_RESET  = 15;
    static const unsigned UDP_HEARTBEAT_RX_CNT_RESET          = 60;
    static const unsigned TCP_HEARTBEAT_TX_CNT_RESET          = 10;
    static const unsigned TCP_HEARTBEAT_RX_CNT_RESET          = 15;

    Async::Config*                    m_cfg                = nullptr;
    std::string                       m_name               {"SvxPlayer"};
    FramedTcpClient                   m_con;
    Async::EncryptedUdpSocket*        m_udp_sock           = nullptr;
    ReflectorUdpMsg::ClientId         m_client_id          = 0;
    std::string                       m_callsign;
    std::string                       m_auth_key;
    ConState                          m_con_state          = STATE_DISCONNECTED;
    Async::Timer                      m_reconnect_timer;
    UdpCipher::IVCntr                 m_next_udp_rx_seq    = 0;
    Async::Timer                      m_heartbeat_timer;
    Async::Timer                      m_flush_timeout_timer;
    unsigned                          m_udp_heartbeat_tx_cnt_reset
                                        = DEFAULT_UDP_HEARTBEAT_TX_CNT_RESET;
    unsigned                          m_udp_heartbeat_tx_cnt = 0;
    unsigned                          m_udp_heartbeat_rx_cnt = 0;
    unsigned                          m_tcp_heartbeat_tx_cnt = 0;
    unsigned                          m_tcp_heartbeat_rx_cnt = 0;
    struct timeval                    m_last_talker_timestamp {};

    Async::AudioEncoder*              m_enc                = nullptr;
    Async::AudioDecoder*              m_dec                = nullptr;
    Async::AudioPacer*                m_pacer              = nullptr;
    MsgHandler*                       m_msg_handler        = nullptr;

    uint32_t                          m_default_tg         = 0;
    uint32_t                          m_selected_tg        = 0;
    std::string                       m_codec              {"OPUS"};
    bool                              m_verbose            = true;

    std::queue<PlayRequest>           m_play_queue;
    bool                              m_playing            = false;

    Async::Pty*                       m_pty                = nullptr;
    std::string                       m_pty_path;

    Scheduler                         m_scheduler;

    Async::SslContext                 m_ssl_ctx;
    Async::SslKeypair                 m_ssl_pkey;
    Async::SslCertSigningReq          m_ssl_csr;
    Async::SslX509                    m_ssl_cert;
    std::string                       m_pki_dir;
    std::string                       m_cafile;
    std::string                       m_crtfile;
    std::string                       m_keyfile;
    std::string                       m_csrfile;
    bool                              m_download_ca_bundle = true;

    std::vector<uint8_t>              m_udp_cipher_iv_rand;
    UdpCipher::IVCntr                 m_udp_cipher_iv_cntr = 1;
    UdpCipher::AAD                    m_aad;

    SvxPlayer(const SvxPlayer&);
    SvxPlayer& operator=(const SvxPlayer&);

    bool setupPki(void);
    bool setupConnection(void);
    bool setupScheduler(void);
    bool setupPty(void);

    void onConnected(void);
    void onDisconnected(Async::TcpConnection* con,
                        Async::TcpConnection::DisconnectReason reason);
    bool onVerifyPeer(Async::TcpConnection* con, bool preverify_ok,
                      X509_STORE_CTX* x509_store_ctx);
    void onSslConnectionReady(Async::TcpConnection* con);
    void onFrameReceived(Async::FramedTcpConnection* con,
                         std::vector<uint8_t>& data);

    void handleMsgError(std::istream& is);
    void handleMsgProtoVerDowngrade(std::istream& is);
    void handleMsgAuthChallenge(std::istream& is);
    void handleMsgAuthOk(void);
    void handleMsgCAInfo(std::istream& is);
    void handleMsgCABundle(std::istream& is);
    void handleMsgStartEncryption(void);
    void handleMsgClientCsrRequest(void);
    void handleMsgClientCert(std::istream& is);
    void handleMsgServerInfo(std::istream& is);
    void handlMsgStartUdpEncryption(std::istream& is);

    void sendMsg(const ReflectorMsg& msg);
    void sendEncodedAudio(const void* buf, int count);
    void flushEncodedAudio(void);
    bool udpCipherDataReceived(const Async::IpAddress& addr, uint16_t port,
                               void* buf, int count);
    void udpDatagramReceived(const Async::IpAddress& addr, uint16_t port,
                             void* aad, void* buf, int count);
    void sendUdpMsg(const UdpCipher::AAD& aad, const ReflectorUdpMsg& msg);
    void sendUdpMsg(const ReflectorUdpMsg& msg);
    void sendUdpRegisterMsg(void);

    void connect(void);
    void disconnect(void);
    void reconnect(void);
    bool isConnected(void) const;
    bool isLoggedIn(void) const { return m_con_state == STATE_CONNECTED; }

    bool setAudioCodec(const std::string& codec_name);
    bool codecIsAvailable(const std::string& codec_name);

    void handleTimerTick(Async::Timer* t);
    void flushTimeout(Async::Timer* t = nullptr);
    void allMsgsWritten(void);
    void allEncodedSamplesFlushed(void);

    void selectTg(uint32_t tg);
    void startNextPlayback(void);

    bool loadClientCertificate(void);

    void onPtyData(const void* buf, size_t len);
    void processCommand(const std::string& line);
};


#endif /* SVX_PLAYER_INCLUDED */

/*
 * This file has not been truncated
 */
