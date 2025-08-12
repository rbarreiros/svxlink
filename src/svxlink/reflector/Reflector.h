/**
@file	 Reflector.h
@brief   The main reflector class
@author  Tobias Blomberg / SM0SVX
@date	 2017-02-11

\verbatim
SvxReflector - An audio reflector for connecting SvxLink Servers
Copyright (C) 2003-2024 Tobias Blomberg / SM0SVX

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

#ifndef REFLECTOR_INCLUDED
#define REFLECTOR_INCLUDED


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <sigc++/sigc++.h>
#include <sys/time.h>
#include <vector>
#include <string>
#include <json/json.h>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncTcpServer.h>
#include <AsyncFramedTcpConnection.h>
#include <AsyncTimer.h>
#include <AsyncAtTimer.h>
#include <AsyncHttpServerConnection.h>
#include <AsyncExec.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "ProtoVer.h"
#include "ReflectorClient.h"

#ifdef HAVE_MQTT
#include "MqttHandler.h"

// 1 minute
#define MQTT_HEARTBEAT_INTERVAL 60000U
#endif


/****************************************************************************
 *
 * Forward declarations
 *
 ****************************************************************************/

namespace Async
{
  class EncryptedUdpSocket;
  class Config;
  class Pty;
};

class ReflectorMsg;
class ReflectorUdpMsg;


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
@brief	The main reflector class
@author Tobias Blomberg / SM0SVX
@date   2017-02-11

This is the main class for the reflector. It handles all network traffic and
the dispatching of incoming messages to the correct ReflectorClient object.
*/
class Reflector : public sigc::trackable
{
  public:
    static time_t timeToRenewCert(const Async::SslX509& cert);

    /**
     * @brief 	Default constructor
     */
    Reflector(void);

    /**
     * @brief 	Destructor
     */
    ~Reflector(void);

    /**
     * @brief 	Initialize the reflector
     * @param 	cfg A previously initialized configuration object
     * @return	Return \em true on success or else \em false
     */
    bool initialize(Async::Config &cfg);

    /**
     * @brief   Return a list of all connected nodes
     * @param   nodes The vector to return the result in
     *
     * This function is used to get a list of the callsigns of all connected
     * nodes.
     */
    void nodeList(std::vector<std::string>& nodes) const;

    /**
     * @brief   Broadcast a TCP message to connected clients
     * @param   msg The message to broadcast
     * @param   filter The client filter to apply
     *
     * This function is used to broadcast a message to all connected clients,
     * possibly applying a client filter.  The message is not really a IP
     * broadcast but rather unicast to all connected clients.
     */
    void broadcastMsg(const ReflectorMsg& msg,
        const ReflectorClient::Filter& filter=ReflectorClient::NoFilter());

    /**
     * @brief   Send a UDP datagram to the specificed ReflectorClient
     * @param   client The client to the send datagram to
     * @param   buf The payload to send
     * @param   count The number of bytes in the payload
     * @return  Returns \em true on success or else \em false
     */
    bool sendUdpDatagram(ReflectorClient *client, const ReflectorUdpMsg& msg);

    void broadcastUdpMsg(const ReflectorUdpMsg& msg,
        const ReflectorClient::Filter& filter=ReflectorClient::NoFilter());

    /**
     * @brief   Get the TG for protocol V1 clients
     * @return  Returns the TG used for protocol V1 clients
     */
    uint32_t tgForV1Clients(void) { return m_tg_for_v1_clients; }

    /**
     * @brief   Request QSY to another talk group
     * @param   tg The talk group to QSY to
     */
    void requestQsy(ReflectorClient *client, uint32_t tg);

    Async::EncryptedUdpSocket* udpSocket(void) const { return m_udp_sock; }

    uint32_t randomQsyLo(void) const { return m_random_qsy_lo; }
    uint32_t randomQsyHi(void) const { return m_random_qsy_hi; }

    Async::SslCertSigningReq loadClientPendingCsr(const std::string& callsign);
    Async::SslCertSigningReq loadClientCsr(const std::string& callsign);
    bool renewedClientCert(Async::SslX509& cert);
    bool signClientCert(Async::SslX509& cert, const std::string& ca_op);
    Async::SslX509 signClientCsr(const std::string& cn);
    Async::SslX509 loadClientCertificate(const std::string& callsign);

    size_t caSize(void) const { return m_ca_size; }
    const std::vector<uint8_t>& caDigest(void) const { return m_ca_md; }
    const std::vector<uint8_t>& caSignature(void) const { return m_ca_sig; }
    std::string clientCertPem(const std::string& callsign) const;
    std::string caBundlePem(void) const;
    std::string issuingCertPem(void) const;
    bool callsignOk(const std::string& callsign) const;
    bool reqEmailOk(const Async::SslCertSigningReq& req) const;
    bool emailOk(const std::string& email) const;
    std::string checkCsr(const Async::SslCertSigningReq& req);
    Async::SslX509 csrReceived(Async::SslCertSigningReq& req);

    Json::Value& clientStatus(const std::string& callsign);

#ifdef HAVE_MQTT
    void updateConnectedNodes();
#endif

  protected:

  private:
    typedef std::map<Async::FramedTcpConnection*,
                     ReflectorClient*> ReflectorClientConMap;
    typedef Async::TcpServer<Async::FramedTcpConnection> FramedTcpServer;
    using HttpServer = Async::TcpServer<Async::HttpServerConnection>;

    static constexpr unsigned ROOT_CA_VALIDITY_DAYS     = 25*365;
    static constexpr unsigned ISSUING_CA_VALIDITY_DAYS  = 4*90;
    static constexpr unsigned CERT_VALIDITY_DAYS        = 90;
    static constexpr int      CERT_VALIDITY_OFFSET_DAYS = -1;

    FramedTcpServer*            m_srv;
    Async::EncryptedUdpSocket*  m_udp_sock;
    ReflectorClientConMap       m_client_con_map;
    Async::Config*              m_cfg;
    uint32_t                    m_tg_for_v1_clients;
    uint32_t                    m_random_qsy_lo;
    uint32_t                    m_random_qsy_hi;
    uint32_t                    m_random_qsy_tg;
    HttpServer*                 m_http_server;
    Async::Pty*                 m_cmd_pty;
    Async::SslContext           m_ssl_ctx;
    std::string                 m_keys_dir;
    std::string                 m_pending_csrs_dir;
    std::string                 m_csrs_dir;
    std::string                 m_certs_dir;
    UdpCipher::AAD              m_aad;
    Async::SslKeypair           m_ca_pkey;
    Async::SslX509              m_ca_cert;
    Async::SslKeypair           m_issue_ca_pkey;
    Async::SslX509              m_issue_ca_cert;
    std::string                 m_pki_dir;
    std::string                 m_ca_bundle_file;
    std::string                 m_crtfile;
    Async::AtTimer              m_renew_cert_timer;
    Async::AtTimer              m_renew_issue_ca_cert_timer;
    size_t                      m_ca_size = 0;
    std::vector<uint8_t>        m_ca_md;
    std::vector<uint8_t>        m_ca_sig;
    std::string                 m_accept_cert_email;
    Json::Value                 m_status;

    // Configuration state file management
    Async::Config*              m_state_cfg;
    std::string                 m_state_file_path;
    std::string                 m_original_config_file;

#ifdef HAVE_MQTT
    // MQTT handler class
    std::unique_ptr<MqttHandler> m_mqtt_handler;

    // MQTT configuration
    bool m_mqtt_enabled;
    
    // MQTT heartbeat timer LWP 
    Async::Timer m_mqtt_heartbeat_timer;
    time_t m_start_time;

    std::string m_mqtt_broker_host;
    int m_mqtt_broker_port;
    std::string m_mqtt_username;
    std::string m_mqtt_password;
    std::string m_mqtt_topic_prefix;
    bool m_mqtt_ssl_enabled;
    std::string m_mqtt_ca_cert_file;
    std::string m_mqtt_client_cert_file;
    std::string m_mqtt_client_key_file;
    bool m_mqtt_ssl_verify_hostname;
    
    // MQTT methods
    bool initMqtt();
    void handleMqttCommand(const std::string& command);
    void publishStatusToMqtt();
    void publishMqttHeartbeat();
    std::string generateMqttClientId() const;
#endif

    Reflector(const Reflector&);
    Reflector& operator=(const Reflector&);
    void clientConnected(Async::FramedTcpConnection *con);
    void clientDisconnected(Async::FramedTcpConnection *con,
                            Async::FramedTcpConnection::DisconnectReason reason);
    bool udpCipherDataReceived(const Async::IpAddress& addr, uint16_t port,
                               void *buf, int count);
    void udpDatagramReceived(const Async::IpAddress& addr, uint16_t port,
                             void* aad, void *buf, int count);
    void onTalkerUpdated(uint32_t tg, ReflectorClient* old_talker,
                         ReflectorClient *new_talker);
    void httpRequestReceived(Async::HttpServerConnection *con,
                             Async::HttpServerConnection::Request& req);
    void httpClientConnected(Async::HttpServerConnection *con);
    void httpClientDisconnected(Async::HttpServerConnection *con,
        Async::HttpServerConnection::DisconnectReason reason);
    void onRequestAutoQsy(uint32_t from_tg);
    uint32_t nextRandomQsyTg(void);
    void ctrlPtyDataReceived(const void *buf, size_t count);
    void cfgUpdated(const std::string& section, const std::string& tag);
    bool loadCertificateFiles(void);
    bool loadServerCertificateFiles(void);
    bool generateKeyFile(Async::SslKeypair& pkey, const std::string& keyfile);
    bool loadRootCAFiles(void);
    bool loadSigningCAFiles(void);
    bool onVerifyPeer(Async::TcpConnection *con, bool preverify_ok,
                      X509_STORE_CTX *x509_store_ctx);
    bool buildPath(const std::string& sec, const std::string& tag,
                   const std::string& defdir, std::string& defpath);
    bool removeClientCert(const std::string& cn);
    void runCAHook(const Async::Exec::Environment& env);

    // Configuration state file management
    bool loadStateFile(void);
    bool saveStateFile(void);
    bool discardStateFile(void);
    void showConfigDiff(void);

    // Command handling methods (shared between PTY and MQTT)
    enum class CommandResult { SUCCESS, ERROR };
    struct CommandResponse {
      CommandResult result;
      std::string message;
    };
    
    CommandResponse handleCfgCommand(std::istringstream& ss);
    CommandResponse handleNodeCommand(std::istringstream& ss);
    CommandResponse handleCaCommand(std::istringstream& ss);
    CommandResponse handleCfgSaveCommand();
    CommandResponse handleCfgDiscardCommand();
    CommandResponse handleCfgShowCommand();

};  /* class Reflector */


#endif /* REFLECTOR_INCLUDED */



/*
 * This file has not been truncated
 */
