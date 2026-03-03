/**
@file    ReflectorClient.cpp
@brief   A standalone base class for connecting to the SvxReflector
@author  Rui Barreiros based on ReflectorLogic by Tobias Blomberg / SM0SVX

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

#include <unistd.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#include <sstream>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <numeric>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncApplication.h>
#include <AsyncDigest.h>
#include <AsyncSslKeypair.h>
#include <AsyncSslCertSigningReq.h>
#include <AsyncEncryptedUdpSocket.h>
#include <AsyncIpAddress.h>
#include <version/SVXLINK.h>
#include <config.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "ReflectorClient.h"
#include "../reflector/ProtoVer.h"


/****************************************************************************
 *
 * Namespaces to use
 *
 ****************************************************************************/

using namespace std;
using namespace Async;


/****************************************************************************
 *
 * Local class definitions
 *
 ****************************************************************************/

namespace {
  MsgProtoVer proto_ver;
};


/****************************************************************************
 *
 * Public member functions
 *
 ****************************************************************************/

ReflectorClient::ReflectorClient(void)
  : m_reconnect_timer(60000, Timer::TYPE_ONESHOT, false),
    m_heartbeat_timer(1000, Timer::TYPE_PERIODIC, false),
    m_flush_timeout_timer(3000, Timer::TYPE_ONESHOT, false)
{
  timerclear(&m_last_talker_timestamp);

  m_reconnect_timer.expired.connect(
      sigc::hide(sigc::mem_fun(*this, &ReflectorClient::reconnect)));
  m_heartbeat_timer.expired.connect(
      sigc::mem_fun(*this, &ReflectorClient::handleTimerTick));
  m_flush_timeout_timer.expired.connect(
      sigc::mem_fun(*this, &ReflectorClient::flushTimeout));

  m_con.connected.connect(
      sigc::mem_fun(*this, &ReflectorClient::onTcpConnected));
  m_con.disconnected.connect(
      sigc::mem_fun(*this, &ReflectorClient::onTcpDisconnected));
  m_con.frameReceived.connect(
      sigc::mem_fun(*this, &ReflectorClient::onFrameReceived));
  m_con.verifyPeer.connect(
      sigc::mem_fun(*this, &ReflectorClient::onVerifyPeer));
  m_con.sslConnectionReady.connect(
      sigc::mem_fun(*this, &ReflectorClient::onSslConnectionReady));
  m_con.setMaxFrameSize(ReflectorMsg::MAX_POSTAUTH_FRAME_SIZE);
} /* ReflectorClient::ReflectorClient */


ReflectorClient::~ReflectorClient(void)
{
  disconnect();
  delete m_udp_sock;
  m_udp_sock = nullptr;
} /* ReflectorClient::~ReflectorClient */


bool ReflectorClient::initialize(Async::Config& cfg, const std::string& section)
{
  m_section = section;

  cfg.getValue(section, "VERBOSE", m_verbose);

  std::vector<std::string> hosts;
  if (cfg.getValue(section, "HOST", hosts))
  {
    std::cerr << "*** WARNING: The " << section
              << "/HOST configuration variable is deprecated. "
                 "Use HOSTS instead." << std::endl;
  }
  cfg.getValue(section, "HOSTS", hosts);
  std::string srv_domain;
  cfg.getValue(section, "DNS_DOMAIN", srv_domain);
  if (srv_domain.empty() && hosts.empty())
  {
    std::cerr << "*** ERROR: At least one of HOSTS or DNS_DOMAIN must be "
                 "specified in [" << section << "]" << std::endl;
    return false;
  }

  if (!srv_domain.empty())
  {
    m_con.setService("svxreflector", "tcp", srv_domain);
  }
  if (!hosts.empty())
  {
    uint16_t reflector_port = 5300;
    cfg.getValue(section, "HOST_PORT", reflector_port);
    DnsResourceRecordSRV::Prio prio = 100;
    cfg.getValue(section, "HOST_PRIO", prio);
    DnsResourceRecordSRV::Prio prio_inc = 1;
    cfg.getValue(section, "HOST_PRIO_INC", prio_inc);
    DnsResourceRecordSRV::Weight weight = 100 / hosts.size();
    cfg.getValue(section, "HOST_WEIGHT", weight);
    for (const auto& host_spec : hosts)
    {
      std::string host = host_spec;
      uint16_t port = reflector_port;
      auto colon = host.find(':');
      if (colon != std::string::npos)
      {
        host = host_spec.substr(0, colon);
        port = static_cast<uint16_t>(atoi(host_spec.substr(colon + 1).c_str()));
      }
      m_con.addStaticSRVRecord(0, prio, weight, port, host);
      prio += prio_inc;
    }
  }

  if (!setupPki(cfg))
  {
    return false;
  }

  if (!cfg.getValue(section, "CALLSIGN", m_callsign) || m_callsign.empty())
  {
    std::cerr << "*** ERROR: " << section
              << "/CALLSIGN missing or empty" << std::endl;
    return false;
  }

  cfg.getValue(section, "AUTH_KEY", m_auth_key);
  cfg.getValue(section, "UDP_HEARTBEAT_INTERVAL", m_udp_heartbeat_tx_cnt_reset);

  Async::Application::app().runTask([this]{ connect(); });
  return true;
} /* ReflectorClient::initialize */


void ReflectorClient::selectTg(uint32_t tg)
{
  if (tg != m_selected_tg)
  {
    std::cout << m_section << ": Selecting TG #" << tg << std::endl;
    m_selected_tg = tg;
    if (isLoggedIn())
    {
      sendMsg(MsgSelectTG(tg));
    }
  }
} /* ReflectorClient::selectTg */


void ReflectorClient::monitorTgs(const std::set<uint32_t>& tgs)
{
  m_monitor_tgs = tgs;
  if (isLoggedIn())
  {
    sendMsg(MsgTgMonitor(m_monitor_tgs));
  }
} /* ReflectorClient::monitorTgs */


void ReflectorClient::sendEncodedAudio(const void* buf, int count)
{
  if (!isLoggedIn())
  {
    return;
  }
  if (m_flush_timeout_timer.isEnabled())
  {
    m_flush_timeout_timer.setEnable(false);
  }
  sendUdpMsg(MsgUdpAudio(buf, count));
} /* ReflectorClient::sendEncodedAudio */


void ReflectorClient::flushEncodedAudio(void)
{
  if (!isLoggedIn())
  {
    flushTimeout();
    return;
  }
  sendUdpMsg(MsgUdpFlushSamples());
  m_flush_timeout_timer.setEnable(true);
} /* ReflectorClient::flushEncodedAudio */


bool ReflectorClient::isConnected(void) const
{
  return m_con.isConnected();
} /* ReflectorClient::isConnected */


/****************************************************************************
 *
 * Protected member functions
 *
 ****************************************************************************/

void ReflectorClient::onLoggedIn(void)
{
  if (m_selected_tg > 0)
  {
    sendMsg(MsgSelectTG(m_selected_tg));
  }
  if (!m_monitor_tgs.empty())
  {
    sendMsg(MsgTgMonitor(m_monitor_tgs));
  }
} /* ReflectorClient::onLoggedIn */


/****************************************************************************
 *
 * Private member functions
 *
 ****************************************************************************/

void ReflectorClient::onTcpConnected(void)
{
  std::cout << "NOTICE[" << m_section << "]: Connected to "
            << m_con.remoteHost() << ":" << m_con.remotePort()
            << " (" << (m_con.isPrimary() ? "primary" : "secondary") << ")"
            << std::endl;
  sendMsg(proto_ver);
  m_udp_heartbeat_tx_cnt = m_udp_heartbeat_tx_cnt_reset;
  m_udp_heartbeat_rx_cnt = UDP_HEARTBEAT_RX_CNT_RESET;
  m_tcp_heartbeat_tx_cnt = TCP_HEARTBEAT_TX_CNT_RESET;
  m_tcp_heartbeat_rx_cnt = TCP_HEARTBEAT_RX_CNT_RESET;
  m_heartbeat_timer.setEnable(true);
  m_next_udp_rx_seq = 0;
  timerclear(&m_last_talker_timestamp);
  m_con_state = STATE_EXPECT_CA_INFO;
  onConnected();
} /* ReflectorClient::onTcpConnected */


void ReflectorClient::onTcpDisconnected(TcpConnection*,
                                        TcpConnection::DisconnectReason reason)
{
  std::cout << m_section << ": Disconnected from "
            << m_con.remoteHost() << ":" << m_con.remotePort() << ": "
            << TcpConnection::disconnectReasonStr(reason) << std::endl;

  m_reconnect_timer.setEnable(reason == TcpConnection::DR_ORDERED_DISCONNECT);
  delete m_udp_sock;
  m_udp_sock = nullptr;
  m_next_udp_rx_seq = 0;
  m_heartbeat_timer.setEnable(false);
  if (m_flush_timeout_timer.isEnabled())
  {
    m_flush_timeout_timer.setEnable(false);
    onAllSamplesFlushed();
  }
  if (timerisset(&m_last_talker_timestamp))
  {
    onAudioFlushed(m_current_audio_tg);
    timerclear(&m_last_talker_timestamp);
  }
  m_con_state = STATE_DISCONNECTED;
  onDisconnected();
} /* ReflectorClient::onTcpDisconnected */


bool ReflectorClient::onVerifyPeer(TcpConnection*, bool preverify_ok,
                                   X509_STORE_CTX* x509_store_ctx)
{
  Async::SslX509 cert(*x509_store_ctx);
  preverify_ok = preverify_ok && !cert.isNull() && !cert.commonName().empty();
  if (!preverify_ok)
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Certificate verification failed for reflector server"
              << std::endl;
  }
  return preverify_ok;
} /* ReflectorClient::onVerifyPeer */


void ReflectorClient::onSslConnectionReady(TcpConnection*)
{
  std::cout << m_section << ": Encrypted connection established" << std::endl;

  if (m_con_state != STATE_EXPECT_SSL_CON_READY)
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Unexpected SSL connection readiness" << std::endl;
    disconnect();
    return;
  }

  if (m_con.sslVerifyResult() != X509_V_OK)
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: SSL Certificate verification failed" << std::endl;
    disconnect();
    return;
  }

  auto peer_cert = m_con.sslPeerCertificate();
  bool cert_match_host = false;
  std::string remote_name = m_con.remoteHostName();
  if (!remote_name.empty())
  {
    if (remote_name.back() == '.')
    {
      remote_name.pop_back();
    }
    cert_match_host |= peer_cert.matchHost(remote_name);
  }
  cert_match_host |= peer_cert.matchIp(m_con.remoteHost());
  if (!cert_match_host)
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Server certificate does not match hostname ("
              << remote_name << ") or IP (" << m_con.remoteHost() << ")"
              << std::endl;
    disconnect();
    return;
  }

  m_con_state = STATE_EXPECT_AUTH_ANSWER;
} /* ReflectorClient::onSslConnectionReady */


void ReflectorClient::onFrameReceived(FramedTcpConnection*,
                                      std::vector<uint8_t>& data)
{
  char* buf = reinterpret_cast<char*>(&data.front());
  int len = data.size();
  stringstream ss;
  ss.write(buf, len);

  ReflectorMsg header;
  if (!header.unpack(ss))
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Unpacking failed for TCP message header" << std::endl;
    disconnect();
    return;
  }

  if ((header.type() >= 100) && (m_con_state < STATE_AUTHENTICATED))
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Unexpected protocol message type=" << header.type()
              << " before authentication" << std::endl;
    disconnect();
    return;
  }

  m_tcp_heartbeat_rx_cnt = TCP_HEARTBEAT_RX_CNT_RESET;

  switch (header.type())
  {
    case MsgHeartbeat::TYPE:
      break;
    case MsgError::TYPE:
      handleMsgError(ss);
      break;
    case MsgProtoVerDowngrade::TYPE:
      handleMsgProtoVerDowngrade(ss);
      break;
    case MsgCAInfo::TYPE:
      handleMsgCAInfo(ss);
      break;
    case MsgStartEncryption::TYPE:
      handleMsgStartEncryption();
      break;
    case MsgCABundle::TYPE:
      handleMsgCABundle(ss);
      break;
    case MsgAuthChallenge::TYPE:
      handleMsgAuthChallenge(ss);
      break;
    case MsgAuthOk::TYPE:
      handleMsgAuthOk();
      break;
    case MsgClientCsrRequest::TYPE:
      handleMsgClientCsrRequest();
      break;
    case MsgClientCert::TYPE:
      handleMsgClientCert(ss);
      break;
    case MsgServerInfo::TYPE:
      handleMsgServerInfo(ss);
      break;
    case MsgNodeList::TYPE:
      handleMsgNodeList(ss);
      break;
    case MsgNodeJoined::TYPE:
      handleMsgNodeJoined(ss);
      break;
    case MsgNodeLeft::TYPE:
      handleMsgNodeLeft(ss);
      break;
    case MsgTalkerStart::TYPE:
      handleMsgTalkerStart(ss);
      break;
    case MsgTalkerStop::TYPE:
      handleMsgTalkerStop(ss);
      break;
    case MsgRequestQsy::TYPE:
      handleMsgRequestQsy(ss);
      break;
    case MsgStartUdpEncryption::TYPE:
      handleMsgStartUdpEncryption(ss);
      break;
    default:
      break;
  }
} /* ReflectorClient::onFrameReceived */


void ReflectorClient::handleMsgError(std::istream& is)
{
  MsgError msg;
  if (!msg.unpack(is))
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Could not unpack MsgError" << std::endl;
    disconnect();
    return;
  }
  std::cerr << "*** ERROR[" << m_section
            << "]: Server error: " << msg.message() << std::endl;
  disconnect();
} /* ReflectorClient::handleMsgError */


void ReflectorClient::handleMsgProtoVerDowngrade(std::istream& is)
{
  MsgProtoVerDowngrade msg;
  if (!msg.unpack(is))
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Could not unpack MsgProtoVerDowngrade" << std::endl;
    disconnect();
    return;
  }
  std::cout << m_section
            << ": Server too old; cannot downgrade from protocol v"
            << proto_ver.majorVer() << "." << proto_ver.minorVer()
            << " to v" << msg.majorVer() << "." << msg.minorVer()
            << std::endl;
  disconnect();
} /* ReflectorClient::handleMsgProtoVerDowngrade */


void ReflectorClient::handleMsgCAInfo(std::istream& is)
{
  if (m_con_state != STATE_EXPECT_CA_INFO)
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Unexpected MsgCAInfo" << std::endl;
    disconnect();
    return;
  }
  MsgCAInfo msg;
  if (!msg.unpack(is))
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Could not unpack MsgCAInfo" << std::endl;
    disconnect();
    return;
  }

  bool request_ca_bundle = false;
  if (m_download_ca_bundle)
  {
    std::ifstream ca_ifs(m_cafile);
    request_ca_bundle = !ca_ifs.good();
    if (ca_ifs.good())
    {
      std::string ca_pem(std::istreambuf_iterator<char>{ca_ifs}, {});
      auto ca_md = Async::Digest().md("sha256", ca_pem);
      if (ca_md != msg.md())
      {
        std::cerr << "*** WARNING[" << m_section
                  << "]: CA bundle outdated; please contact the reflector "
                     "sysop for an updated bundle." << std::endl;
      }
    }
  }
  if (request_ca_bundle)
  {
    sendMsg(MsgCABundleRequest());
    m_con_state = STATE_EXPECT_CA_BUNDLE;
  }
  else
  {
    sendMsg(MsgStartEncryptionRequest());
    m_con_state = STATE_EXPECT_START_ENCRYPTION;
  }
} /* ReflectorClient::handleMsgCAInfo */


void ReflectorClient::handleMsgCABundle(std::istream& is)
{
  if (m_con_state != STATE_EXPECT_CA_BUNDLE)
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Unexpected MsgCABundle" << std::endl;
    disconnect();
    return;
  }
  MsgCABundle msg;
  if (!msg.unpack(is))
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Could not unpack MsgCABundle" << std::endl;
    disconnect();
    return;
  }
  if (msg.caPem().empty())
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Received empty CA bundle" << std::endl;
    disconnect();
    return;
  }

  Async::SslX509 signing_cert;
  signing_cert.readPem(msg.certPem());
  Async::Digest dgst;
  auto signing_cert_pubkey = signing_cert.publicKey();
  bool signature_ok =
    dgst.signVerifyInit(MsgCABundle::MD_ALG, signing_cert_pubkey) &&
    dgst.signVerifyUpdate(msg.caPem()) &&
    dgst.signVerifyFinal(msg.signature());
  if (!signature_ok)
  {
    std::cerr << "*** WARNING[" << m_section
              << "]: Received CA bundle with invalid signature" << std::endl;
    disconnect();
    return;
  }

  std::cout << m_section << ": Writing received CA bundle to '"
            << m_cafile << "'" << std::endl;
  std::ofstream ofs(m_cafile);
  if (ofs.good())
  {
    ofs << msg.caPem();
    ofs.close();
  }
  else
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Could not write CA file '" << m_cafile << "'"
              << std::endl;
  }

  if (!m_ssl_ctx.setCaCertificateFile(m_cafile))
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Failed to reload CA file '" << m_cafile << "'"
              << std::endl;
    disconnect();
    return;
  }

  sendMsg(MsgStartEncryptionRequest());
  m_con_state = STATE_EXPECT_START_ENCRYPTION;
} /* ReflectorClient::handleMsgCABundle */


void ReflectorClient::handleMsgStartEncryption(void)
{
  if (m_con_state != STATE_EXPECT_START_ENCRYPTION)
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Unexpected MsgStartEncryption" << std::endl;
    disconnect();
    return;
  }
  std::cout << m_section << ": Setting up encrypted channel" << std::endl;
  m_con.enableSsl(true);
  m_con_state = STATE_EXPECT_SSL_CON_READY;
} /* ReflectorClient::handleMsgStartEncryption */


void ReflectorClient::handleMsgAuthChallenge(std::istream& is)
{
  if (m_con_state != STATE_EXPECT_AUTH_ANSWER)
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Unexpected MsgAuthChallenge" << std::endl;
    disconnect();
    return;
  }
  MsgAuthChallenge msg;
  if (!msg.unpack(is))
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Could not unpack MsgAuthChallenge" << std::endl;
    disconnect();
    return;
  }
  const uint8_t* challenge = msg.challenge();
  if (challenge == nullptr)
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Illegal challenge received" << std::endl;
    disconnect();
    return;
  }
  sendMsg(MsgAuthResponse(m_callsign, m_auth_key, challenge));
} /* ReflectorClient::handleMsgAuthChallenge */


void ReflectorClient::handleMsgAuthOk(void)
{
  if (m_con_state != STATE_EXPECT_AUTH_ANSWER)
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Unexpected MsgAuthOk" << std::endl;
    disconnect();
    return;
  }
  std::cout << m_section << ": Authentication OK" << std::endl;
  m_con_state = STATE_EXPECT_SERVER_INFO;

  auto cert = m_con.sslCertificate();
  if (!cert.isNull())
  {
    struct stat csrst, crtst;
    if ((stat(m_csrfile.c_str(), &csrst) == 0) &&
        (stat(m_crtfile.c_str(), &crtst) == 0) &&
        (csrst.st_mtim.tv_sec > crtst.st_mtim.tv_sec))
    {
      std::cout << m_section
                << ": CSR is newer than certificate; sending CSR to server."
                << std::endl;
      sendMsg(MsgClientCsr(m_ssl_csr.pem()));
    }
  }
} /* ReflectorClient::handleMsgAuthOk */


void ReflectorClient::handleMsgClientCsrRequest(void)
{
  if (m_con_state != STATE_EXPECT_AUTH_ANSWER)
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Unexpected MsgClientCsrRequest" << std::endl;
    disconnect();
    return;
  }
  std::cout << m_section << ": Sending Certificate Signing Request to server"
            << std::endl;
  sendMsg(MsgClientCsr(m_ssl_csr.pem()));
} /* ReflectorClient::handleMsgClientCsrRequest */


void ReflectorClient::handleMsgClientCert(std::istream& is)
{
  if (m_con_state < STATE_EXPECT_AUTH_ANSWER)
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Unexpected MsgClientCert" << std::endl;
    disconnect();
    return;
  }
  MsgClientCert msg;
  if (!msg.unpack(is))
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Could not unpack MsgClientCert" << std::endl;
    disconnect();
    return;
  }
  if (msg.certPem().empty())
  {
    std::cout << m_section << ": Received empty certificate." << std::endl;
    disconnect();
    return;
  }

  std::cout << m_section << ": Received certificate from server" << std::endl;
  Async::SslX509 cert;
  if (!cert.readPem(msg.certPem()) || cert.isNull())
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Failed to parse certificate PEM from server" << std::endl;
    disconnect();
    return;
  }
  if (cert.publicKey() != m_ssl_csr.publicKey())
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Received certificate does not match our private key"
              << std::endl;
    disconnect();
    return;
  }

  std::ofstream ofs(m_crtfile);
  if (!ofs.good() || !(ofs << msg.certPem()))
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Failed to write certificate to '" << m_crtfile << "'"
              << std::endl;
    disconnect();
    return;
  }
  ofs.close();

  if (!loadClientCertificate())
  {
    std::cout << m_section << ": Failed to load received client certificate."
              << std::endl;
    disconnect();
    return;
  }
  reconnect();
} /* ReflectorClient::handleMsgClientCert */


void ReflectorClient::handleMsgServerInfo(std::istream& is)
{
  if (m_con_state != STATE_EXPECT_SERVER_INFO)
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Unexpected MsgServerInfo" << std::endl;
    disconnect();
    return;
  }
  MsgServerInfo msg;
  if (!msg.unpack(is))
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Could not unpack MsgServerInfo" << std::endl;
    disconnect();
    return;
  }
  m_client_id = msg.clientId();

  std::cout << m_section << ": Connected nodes: ";
  const vector<string>& nodes = msg.nodes();
  if (!nodes.empty())
  {
    auto it = nodes.begin();
    std::cout << *it++;
    for (; it != nodes.end(); ++it)
    {
      std::cout << ", " << *it;
    }
  }
  std::cout << std::endl;

    // Pick the first codec offered by the server.
    // We forward raw bytes to the application layer, so no local
    // encoder/decoder check is required here.
  if (!msg.codecs().empty())
  {
    m_codec = msg.codecs().front();
  }
  else
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: No codec offered by server" << std::endl;
    disconnect();
    return;
  }
  std::cout << m_section << ": Using audio codec \"" << m_codec << "\""
            << std::endl;

  const auto cipher = EncryptedUdpSocket::fetchCipher(UdpCipher::NAME);
  if (cipher == nullptr)
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Unsupported UDP cipher " << UdpCipher::NAME << std::endl;
    disconnect();
    return;
  }

  delete m_udp_sock;
  m_udp_cipher_iv_cntr = 1;
  m_udp_sock = new Async::EncryptedUdpSocket;
  m_udp_cipher_iv_rand.resize(UdpCipher::IVRANDLEN);
  const char* err = "unknown";
  if ((err="memory",        m_udp_sock == nullptr) ||
      (err="init",         !m_udp_sock->initOk()) ||
      (err="cipher",       !m_udp_sock->setCipher(cipher)) ||
      (err="iv-rand",      !EncryptedUdpSocket::randomBytes(m_udp_cipher_iv_rand)) ||
      (err="key",          !m_udp_sock->setCipherKey()))
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Could not create UDP socket: " << err << std::endl;
    delete m_udp_sock;
    m_udp_sock = nullptr;
    disconnect();
    return;
  }
  m_udp_sock->setCipherAADLength(UdpCipher::AADLEN);
  m_udp_sock->setTagLength(UdpCipher::TAGLEN);
  m_udp_sock->cipherDataReceived.connect(
      sigc::mem_fun(*this, &ReflectorClient::udpCipherDataReceived));
  m_udp_sock->dataReceived.connect(
      sigc::mem_fun(*this, &ReflectorClient::udpDatagramReceived));

  m_con_state = STATE_EXPECT_START_UDP_ENCRYPTION;

    // Build and send MsgNodeInfo
  Json::Value node_info = buildNodeInfo();
  node_info["sw"] = "SvxReflectorClient";
  node_info["swVer"] = SVXLINK_APP_VERSION;
  node_info["projVer"] = PROJECT_VERSION;
  struct utsname os_info{};
  if (uname(&os_info) == 0)
  {
    node_info["machineArch"] = os_info.machine;
  }

  Json::StreamWriterBuilder builder;
  builder["commentStyle"] = "None";
  builder["indentation"] = "";
  std::ostringstream node_info_os;
  std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
  writer->write(node_info, &node_info_os);

  sendMsg(MsgNodeInfo(m_udp_cipher_iv_rand, m_udp_sock->cipherKey(),
                      node_info_os.str()));
} /* ReflectorClient::handleMsgServerInfo */


void ReflectorClient::handleMsgNodeList(std::istream& is)
{
  MsgNodeList msg;
  if (!msg.unpack(is))
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Could not unpack MsgNodeList" << std::endl;
    disconnect();
    return;
  }
  if (m_verbose)
  {
    std::cout << m_section << ": Connected nodes: ";
    const vector<string>& nodes = msg.nodes();
    if (!nodes.empty())
    {
      auto it = nodes.begin();
      std::cout << *it++;
      for (; it != nodes.end(); ++it) { std::cout << ", " << *it; }
    }
    std::cout << std::endl;
  }
} /* ReflectorClient::handleMsgNodeList */


void ReflectorClient::handleMsgNodeJoined(std::istream& is)
{
  MsgNodeJoined msg;
  if (!msg.unpack(is))
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Could not unpack MsgNodeJoined" << std::endl;
    disconnect();
    return;
  }
  if (m_verbose)
  {
    std::cout << m_section << ": Node joined: " << msg.callsign() << std::endl;
  }
  onNodeJoined(msg.callsign());
} /* ReflectorClient::handleMsgNodeJoined */


void ReflectorClient::handleMsgNodeLeft(std::istream& is)
{
  MsgNodeLeft msg;
  if (!msg.unpack(is))
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Could not unpack MsgNodeLeft" << std::endl;
    disconnect();
    return;
  }
  if (m_verbose)
  {
    std::cout << m_section << ": Node left: " << msg.callsign() << std::endl;
  }
  onNodeLeft(msg.callsign());
} /* ReflectorClient::handleMsgNodeLeft */


void ReflectorClient::handleMsgTalkerStart(std::istream& is)
{
  MsgTalkerStart msg;
  if (!msg.unpack(is))
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Could not unpack MsgTalkerStart" << std::endl;
    disconnect();
    return;
  }
  std::cout << m_section << ": Talker start on TG #" << msg.tg()
            << ": " << msg.callsign() << std::endl;
  m_current_audio_tg = msg.tg();
  onTalkerStart(msg.tg(), msg.callsign());
} /* ReflectorClient::handleMsgTalkerStart */


void ReflectorClient::handleMsgTalkerStop(std::istream& is)
{
  MsgTalkerStop msg;
  if (!msg.unpack(is))
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Could not unpack MsgTalkerStop" << std::endl;
    disconnect();
    return;
  }
  std::cout << m_section << ": Talker stop on TG #" << msg.tg()
            << ": " << msg.callsign() << std::endl;
  onTalkerStop(msg.tg(), msg.callsign());
} /* ReflectorClient::handleMsgTalkerStop */


void ReflectorClient::handleMsgRequestQsy(std::istream& is)
{
  MsgRequestQsy msg;
  if (!msg.unpack(is))
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Could not unpack MsgRequestQsy" << std::endl;
    disconnect();
    return;
  }
  std::cout << m_section << ": Server QSY request for TG #" << msg.tg()
            << std::endl;
  onQsyRequest(msg.tg());
} /* ReflectorClient::handleMsgRequestQsy */


void ReflectorClient::handleMsgStartUdpEncryption(std::istream& is)
{
  if (m_con_state != STATE_EXPECT_START_UDP_ENCRYPTION)
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Unexpected MsgStartUdpEncryption" << std::endl;
    disconnect();
    return;
  }
  MsgStartUdpEncryption msg;
  if (!msg.unpack(is))
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Could not unpack MsgStartUdpEncryption" << std::endl;
    disconnect();
    return;
  }
  m_con_state = STATE_EXPECT_UDP_HEARTBEAT;
  sendUdpRegisterMsg();
} /* ReflectorClient::handleMsgStartUdpEncryption */


bool ReflectorClient::udpCipherDataReceived(const IpAddress&, uint16_t port,
                                            void* buf, int count)
{
  if (static_cast<size_t>(count) < UdpCipher::AADLEN)
  {
    return true;
  }
  stringstream ss;
  ss.write(reinterpret_cast<const char*>(buf), UdpCipher::AADLEN);
  if (!m_aad.unpack(ss))
  {
    std::cerr << "*** WARNING[" << m_section
              << "]: Unpacking UDP AAD failed" << std::endl;
    return true;
  }
  m_udp_sock->setCipherIV(UdpCipher::IV{m_udp_cipher_iv_rand, 0,
                                        m_aad.iv_cntr});
  return false;
} /* ReflectorClient::udpCipherDataReceived */


void ReflectorClient::udpDatagramReceived(const IpAddress& addr, uint16_t port,
                                          void*, void* buf, int count)
{
  if (m_con_state < STATE_EXPECT_START_UDP_ENCRYPTION)
  {
    return;
  }
  if (addr != m_con.remoteHost())
  {
    std::cerr << "*** WARNING[" << m_section
              << "]: UDP from unexpected address " << addr << std::endl;
    return;
  }
  if (port != m_con.remotePort())
  {
    std::cerr << "*** WARNING[" << m_section
              << "]: UDP from unexpected port " << port << std::endl;
    return;
  }

  stringstream ss;
  ss.write(reinterpret_cast<const char*>(buf), count);
  ReflectorUdpMsg header;
  if (!header.unpack(ss))
  {
    std::cerr << "*** WARNING[" << m_section
              << "]: Unpacking UDP message header failed" << std::endl;
    return;
  }

  if (m_aad.iv_cntr < m_next_udp_rx_seq)
  {
    std::cout << m_section << ": Dropping out-of-sequence UDP frame seq="
              << m_aad.iv_cntr << std::endl;
    return;
  }
  m_next_udp_rx_seq = m_aad.iv_cntr + 1;
  m_udp_heartbeat_rx_cnt = UDP_HEARTBEAT_RX_CNT_RESET;

  if ((m_con_state == STATE_EXPECT_UDP_HEARTBEAT) &&
      (header.type() == MsgUdpHeartbeat::TYPE))
  {
    std::cout << m_section << ": Bidirectional UDP communication verified"
              << std::endl;
    m_con.markAsEstablished();
    m_con_state = STATE_CONNECTED;
    onLoggedIn();
    return;
  }

  if (!isLoggedIn())
  {
    return;
  }

  switch (header.type())
  {
    case MsgUdpHeartbeat::TYPE:
      break;

    case MsgUdpAudio::TYPE:
    {
      MsgUdpAudio msg;
      if (!msg.unpack(ss))
      {
        std::cerr << "*** WARNING[" << m_section
                  << "]: Could not unpack MsgUdpAudio" << std::endl;
        return;
      }
      if (!msg.audioData().empty())
      {
        gettimeofday(&m_last_talker_timestamp, nullptr);
        onAudioReceived(m_current_audio_tg, m_codec,
                        msg.audioData().data(), msg.audioData().size());
      }
      break;
    }

    case MsgUdpFlushSamples::TYPE:
      onAudioFlushed(m_current_audio_tg);
      timerclear(&m_last_talker_timestamp);
      break;

    case MsgUdpAllSamplesFlushed::TYPE:
      onAllSamplesFlushed();
      break;

    default:
      break;
  }
} /* ReflectorClient::udpDatagramReceived */


void ReflectorClient::sendMsg(const ReflectorMsg& msg)
{
  if (!isConnected())
  {
    return;
  }
  if ((msg.type() >= 100) && (m_con_state < STATE_AUTHENTICATED))
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Trying to send user message " << msg.type()
              << " before authentication" << std::endl;
    return;
  }

  m_tcp_heartbeat_tx_cnt = TCP_HEARTBEAT_TX_CNT_RESET;

  ostringstream ss;
  ReflectorMsg header(msg.type());
  if (!header.pack(ss) || !msg.pack(ss))
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Failed to pack TCP message" << std::endl;
    disconnect();
    return;
  }
  if (m_con.write(ss.str().data(), ss.str().size()) == -1)
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Failed to write to TCP connection" << std::endl;
    disconnect();
  }
} /* ReflectorClient::sendMsg */


void ReflectorClient::sendUdpMsg(const UdpCipher::AAD& aad,
                                 const ReflectorUdpMsg& msg)
{
  m_udp_heartbeat_tx_cnt = m_udp_heartbeat_tx_cnt_reset;
  if (m_udp_sock == nullptr)
  {
    return;
  }
  ReflectorUdpMsg header(msg.type());
  ostringstream ss;
  if (!header.pack(ss) || !msg.pack(ss))
  {
    std::cerr << "*** ERROR[" << m_section
              << "]: Failed to pack UDP message" << std::endl;
    return;
  }
  m_udp_sock->setCipherIV(
      UdpCipher::IV{m_udp_cipher_iv_rand, m_client_id, aad.iv_cntr});
  std::ostringstream adss;
  if (!aad.pack(adss))
  {
    std::cerr << "*** WARNING[" << m_section
              << "]: Failed to pack UDP AAD" << std::endl;
    return;
  }
  m_udp_sock->write(m_con.remoteHost(), m_con.remotePort(),
                    adss.str().data(), adss.str().size(),
                    ss.str().data(), ss.str().size());
} /* ReflectorClient::sendUdpMsg */


void ReflectorClient::sendUdpMsg(const ReflectorUdpMsg& msg)
{
  if (!isLoggedIn())
  {
    return;
  }
  sendUdpMsg(UdpCipher::AAD{m_udp_cipher_iv_cntr++}, msg);
} /* ReflectorClient::sendUdpMsg */


void ReflectorClient::sendUdpRegisterMsg(void)
{
  sendUdpMsg(UdpCipher::InitialAAD{m_client_id}, MsgUdpHeartbeat());
} /* ReflectorClient::sendUdpRegisterMsg */


void ReflectorClient::connect(void)
{
  if (!isConnected())
  {
    m_reconnect_timer.setEnable(false);
    std::cout << m_section << ": Connecting to "
              << m_con.service() << std::endl;
    m_con.connect();
    m_con.setSslContext(m_ssl_ctx, false);
  }
} /* ReflectorClient::connect */


void ReflectorClient::disconnect(void)
{
  bool was_connected = m_con.isConnected();
  m_con.disconnect();
  if (was_connected)
  {
    onTcpDisconnected(&m_con, TcpConnection::DR_ORDERED_DISCONNECT);
  }
  m_con_state = STATE_DISCONNECTED;
} /* ReflectorClient::disconnect */


void ReflectorClient::reconnect(void)
{
  disconnect();
  connect();
} /* ReflectorClient::reconnect */


void ReflectorClient::handleTimerTick(Async::Timer*)
{
    // Talker audio timeout guard
  if (timerisset(&m_last_talker_timestamp))
  {
    struct timeval now, diff;
    gettimeofday(&now, nullptr);
    timersub(&now, &m_last_talker_timestamp, &diff);
    if (diff.tv_sec > 3)
    {
      std::cout << m_section << ": Last talker audio timeout" << std::endl;
      onAudioFlushed(m_current_audio_tg);
      timerclear(&m_last_talker_timestamp);
    }
  }

  if (--m_udp_heartbeat_tx_cnt == 0)
  {
    if (m_con_state == STATE_EXPECT_UDP_HEARTBEAT)
    {
      sendUdpRegisterMsg();
    }
    else if (isLoggedIn())
    {
      sendUdpMsg(MsgUdpHeartbeat());
    }
  }

  if (--m_tcp_heartbeat_tx_cnt == 0)
  {
    sendMsg(MsgHeartbeat());
  }

  if (--m_udp_heartbeat_rx_cnt == 0)
  {
    std::cout << m_section << ": UDP heartbeat timeout" << std::endl;
    disconnect();
  }

  if (--m_tcp_heartbeat_rx_cnt == 0)
  {
    std::cout << m_section << ": TCP heartbeat timeout" << std::endl;
    disconnect();
  }
} /* ReflectorClient::handleTimerTick */


void ReflectorClient::flushTimeout(Async::Timer*)
{
  m_flush_timeout_timer.setEnable(false);
  onAllSamplesFlushed();
} /* ReflectorClient::flushTimeout */


bool ReflectorClient::setupPki(Async::Config& cfg)
{
  if (!cfg.getValue(m_section, "CERT_PKI_DIR", m_pki_dir) || m_pki_dir.empty())
  {
    m_pki_dir = std::string(SVX_LOCAL_STATE_DIR) + "/pki";
  }
  if (!m_pki_dir.empty() && (access(m_pki_dir.c_str(), F_OK) != 0))
  {
    std::cout << m_section << ": Creating PKI directory \""
              << m_pki_dir << "\"" << std::endl;
    if (mkdir(m_pki_dir.c_str(), 0777) != 0)
    {
      std::cerr << "*** ERROR: Could not create PKI directory \""
                << m_pki_dir << "\"" << std::endl;
      return false;
    }
  }

    // CALLSIGN must be read before PKI for key filenames
  std::string callsign;
  if (!cfg.getValue(m_section, "CALLSIGN", callsign) || callsign.empty())
  {
    std::cerr << "*** ERROR: " << m_section
              << "/CALLSIGN missing or empty (required for PKI setup)"
              << std::endl;
    return false;
  }

  if (!cfg.getValue(m_section, "CERT_KEYFILE", m_keyfile))
  {
    m_keyfile = m_pki_dir + "/" + callsign + ".key";
  }
  if (access(m_keyfile.c_str(), F_OK) != 0)
  {
    std::cout << m_section << ": Generating private key file \""
              << m_keyfile << "\"" << std::endl;
    Async::SslKeypair keypair;
    keypair.generate(2048);
    if (!keypair.writePrivateKeyFile(m_keyfile))
    {
      std::cerr << "*** ERROR: Failed to write private key to \""
                << m_keyfile << "\"" << std::endl;
      return false;
    }
  }
  if (!m_ssl_pkey.readPrivateKeyFile(m_keyfile))
  {
    std::cerr << "*** ERROR: Failed to read private key from \""
              << m_keyfile << "\"" << std::endl;
    return false;
  }

    // Build CSR
  m_ssl_csr.setVersion(Async::SslCertSigningReq::VERSION_1);
  m_ssl_csr.addSubjectName("CN", callsign);
  const std::vector<std::vector<std::string>> subject_names{
    {SN_givenName,              LN_givenName,               "GIVEN_NAME"},
    {SN_surname,                LN_surname,                 "SURNAME"},
    {SN_organizationalUnitName, LN_organizationalUnitName,  "ORG_UNIT"},
    {SN_organizationName,       LN_organizationName,        "ORG"},
    {SN_localityName,           LN_localityName,            "LOCALITY"},
    {SN_stateOrProvinceName,    LN_stateOrProvinceName,     "STATE"},
    {SN_countryName,            LN_countryName,             "COUNTRY"},
  };
  const std::string prefix = "CERT_SUBJ_";
  std::string value;
  for (const auto& snames : subject_names)
  {
    if (std::accumulate(snames.begin(), snames.end(), false,
          [&](bool found, const std::string& cfgsname)
          {
            return found || cfg.getValue(m_section, prefix + cfgsname, value);
          }) && !value.empty())
    {
      m_ssl_csr.addSubjectName(snames[0], value);
    }
  }
  Async::SslX509Extensions csr_exts;
  csr_exts.addBasicConstraints("critical, CA:FALSE");
  csr_exts.addKeyUsage(
      "critical, digitalSignature, keyEncipherment, keyAgreement");
  csr_exts.addExtKeyUsage("clientAuth");
  std::vector<std::string> cert_email;
  if (cfg.getValue(m_section, "CERT_EMAIL", cert_email) && !cert_email.empty())
  {
    std::string csr_san;
    for (const auto& email : cert_email)
    {
      if (!csr_san.empty()) { csr_san += ","; }
      csr_san += "email:" + email;
    }
    csr_exts.addSubjectAltNames(csr_san);
  }
  m_ssl_csr.addExtensions(csr_exts);
  m_ssl_csr.setPublicKey(m_ssl_pkey);
  m_ssl_csr.sign(m_ssl_pkey);

  if (!cfg.getValue(m_section, "CERT_CSRFILE", m_csrfile))
  {
    m_csrfile = m_pki_dir + "/" + callsign + ".csr";
  }
  Async::SslCertSigningReq existing_csr(nullptr);
  if (!existing_csr.readPemFile(m_csrfile) ||
      !existing_csr.verify(m_ssl_pkey) ||
      (m_ssl_csr.digest() != existing_csr.digest()))
  {
    std::cout << m_section << ": Saving CSR to '" << m_csrfile << "'"
              << std::endl;
    if (!m_ssl_csr.writePemFile(m_csrfile))
    {
      std::cerr << "*** ERROR: Failed to write CSR to '" << m_csrfile << "'"
                << std::endl;
      return false;
    }
  }

  if (!cfg.getValue(m_section, "CERT_CRTFILE", m_crtfile))
  {
    m_crtfile = m_pki_dir + "/" + callsign + ".crt";
  }
  loadClientCertificate();

  cfg.getValue(m_section, "CERT_DOWNLOAD_CA_BUNDLE", m_download_ca_bundle);
  if (!cfg.getValue(m_section, "CERT_CAFILE", m_cafile))
  {
    m_cafile = m_pki_dir + "/ca-bundle.crt";
  }
  if (!m_ssl_ctx.setCaCertificateFile(m_cafile))
  {
    if (m_download_ca_bundle)
    {
      std::cerr << "*** WARNING[" << m_section
                << "]: Failed to read CA file '" << m_cafile
                << "'; will try to retrieve from server." << std::endl;
    }
    else
    {
      std::cerr << "*** ERROR[" << m_section
                << "]: Failed to read CA file '" << m_cafile
                << "' and CERT_DOWNLOAD_CA_BUNDLE is false." << std::endl;
      return false;
    }
  }

  return true;
} /* ReflectorClient::setupPki */


bool ReflectorClient::loadClientCertificate(void)
{
  if (m_ssl_cert.readPemFile(m_crtfile) &&
      !m_ssl_cert.isNull() &&
      m_ssl_cert.timeIsWithinRange())
  {
    if (!m_ssl_ctx.setCertificateFiles(m_keyfile, m_crtfile))
    {
      std::cerr << "*** ERROR: Failed to load key/cert pair from '"
                << m_keyfile << "' / '" << m_crtfile << "'" << std::endl;
      return false;
    }
  }
  return true;
} /* ReflectorClient::loadClientCertificate */


/*
 * This file has not been truncated
 */
