/**
@file    SvxPlayer.cpp
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
#include <iomanip>
#include <algorithm>
#include <iterator>
#include <streambuf>
#include <numeric>
#include <cassert>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <json/json.h>

#include <AsyncApplication.h>
#include <AsyncTcpClient.h>
#include <AsyncDigest.h>
#include <AsyncSslKeypair.h>
#include <AsyncSslCertSigningReq.h>
#include <AsyncSslX509Extensions.h>
#include <AsyncEncryptedUdpSocket.h>
#include <AsyncIpAddress.h>
#include <AsyncAudioPassthrough.h>
#include <version/SVXLINK.h>
#include <config.h>

#include <openssl/x509.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "SvxPlayer.h"


/****************************************************************************
 *
 * Namespaces to use
 *
 ****************************************************************************/

using namespace std;
using namespace Async;


/****************************************************************************
 *
 * Local variables
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

SvxPlayer::SvxPlayer(void)
  : m_reconnect_timer(60000, Timer::TYPE_ONESHOT, false),
    m_heartbeat_timer(1000, Timer::TYPE_PERIODIC, false),
    m_flush_timeout_timer(3000, Timer::TYPE_ONESHOT, false)
{
  m_reconnect_timer.expired.connect(
      sigc::hide(sigc::mem_fun(*this, &SvxPlayer::reconnect)));
  m_heartbeat_timer.expired.connect(
      sigc::mem_fun(*this, &SvxPlayer::handleTimerTick));
  m_flush_timeout_timer.expired.connect(
      sigc::mem_fun(*this, &SvxPlayer::flushTimeout));
  timerclear(&m_last_talker_timestamp);

  m_con.connected.connect(
      sigc::mem_fun(*this, &SvxPlayer::onConnected));
  m_con.disconnected.connect(
      sigc::mem_fun(*this, &SvxPlayer::onDisconnected));
  m_con.frameReceived.connect(
      sigc::mem_fun(*this, &SvxPlayer::onFrameReceived));
  m_con.verifyPeer.connect(
      sigc::mem_fun(*this, &SvxPlayer::onVerifyPeer));
  m_con.sslConnectionReady.connect(
      sigc::mem_fun(*this, &SvxPlayer::onSslConnectionReady));
  m_con.setMaxFrameSize(ReflectorMsg::MAX_POSTAUTH_FRAME_SIZE);
} /* SvxPlayer::SvxPlayer */


SvxPlayer::~SvxPlayer(void)
{
  disconnect();
  delete m_pty;
  m_pty = nullptr;
  if (m_msg_handler != nullptr)
  {
    m_msg_handler->unregisterSink();
  }
  delete m_msg_handler;
  m_msg_handler = nullptr;
  if (m_pacer != nullptr)
  {
    m_pacer->unregisterSink();
  }
  delete m_pacer;
  m_pacer = nullptr;
  delete m_enc;
  m_enc = nullptr;
  delete m_dec;
  m_dec = nullptr;
  delete m_udp_sock;
  m_udp_sock = nullptr;
} /* SvxPlayer::~SvxPlayer */


bool SvxPlayer::initialize(Async::Config& cfg)
{
  m_cfg = &cfg;

  cfg.getValue(m_name, "VERBOSE", m_verbose);

  if (!setupConnection())
  {
    return false;
  }

  if (!setupPki())
  {
    return false;
  }

  cfg.getValue(m_name, "DEFAULT_TG", m_default_tg);
  m_selected_tg = m_default_tg;

  cfg.getValue(m_name, "CODEC", m_codec);

  m_msg_handler = new MsgHandler(INTERNAL_SAMPLE_RATE);
  m_msg_handler->allMsgsWritten.connect(
      sigc::mem_fun(*this, &SvxPlayer::allMsgsWritten));

  m_pacer = new Async::AudioPacer(INTERNAL_SAMPLE_RATE, 320, 0);
  m_msg_handler->registerSink(m_pacer, false);

  if (!setAudioCodec("DUMMY"))
  {
    return false;
  }

  if (!setupScheduler())
  {
    return false;
  }

  if (!setupPty())
  {
    return false;
  }

  Async::Application::app().runTask([&]{ connect(); });

  return true;
} /* SvxPlayer::initialize */


void SvxPlayer::playFile(const string& file, uint32_t tg)
{
  if (tg == 0)
  {
    tg = m_default_tg;
  }
  PlayRequest req{file, tg};
  m_play_queue.push(req);

  if (isLoggedIn() && !m_playing)
  {
    startNextPlayback();
  }
} /* SvxPlayer::playFile */


void SvxPlayer::stop(void)
{
  while (!m_play_queue.empty())
  {
    m_play_queue.pop();
  }
  if (m_playing)
  {
    m_playing = false;
    m_msg_handler->clear();
  }
} /* SvxPlayer::stop */


/****************************************************************************
 *
 * Private member functions
 *
 ****************************************************************************/

bool SvxPlayer::setupConnection(void)
{
  vector<string> hosts;
  if (m_cfg->getValue(m_name, "HOST", hosts))
  {
    cerr << "*** WARNING: " << m_name
         << "/HOST is deprecated. Use HOSTS instead." << endl;
  }
  m_cfg->getValue(m_name, "HOSTS", hosts);
  string srv_domain;
  m_cfg->getValue(m_name, "DNS_DOMAIN", srv_domain);
  if (srv_domain.empty() && hosts.empty())
  {
    cerr << "*** ERROR: At least one of HOSTS or DNS_DOMAIN must be "
            "specified in [" << m_name << "]" << endl;
    return false;
  }

  if (!srv_domain.empty())
  {
    m_con.setService("svxreflector", "tcp", srv_domain);
  }

  if (!hosts.empty())
  {
    uint16_t reflector_port = 5300;
    if (m_cfg->getValue(m_name, "PORT", reflector_port))
    {
      cerr << "*** WARNING: " << m_name
           << "/PORT is deprecated. Use HOST_PORT instead." << endl;
    }
    m_cfg->getValue(m_name, "HOST_PORT", reflector_port);
    DnsResourceRecordSRV::Prio prio = 100;
    m_cfg->getValue(m_name, "HOST_PRIO", prio);
    DnsResourceRecordSRV::Prio prio_inc = 1;
    m_cfg->getValue(m_name, "HOST_PRIO_INC", prio_inc);
    DnsResourceRecordSRV::Weight weight =
        static_cast<DnsResourceRecordSRV::Weight>(100 / hosts.size());
    m_cfg->getValue(m_name, "HOST_WEIGHT", weight);
    for (const auto& host_spec : hosts)
    {
      string host = host_spec;
      uint16_t port = reflector_port;
      auto colon = host.find(':');
      if (colon != string::npos)
      {
        host = host_spec.substr(0, colon);
        port = static_cast<uint16_t>(
            atoi(host_spec.substr(colon + 1).c_str()));
      }
      m_con.addStaticSRVRecord(0, prio, weight, port, host);
      prio += prio_inc;
    }
  }

  if (!m_cfg->getValue(m_name, "CALLSIGN", m_callsign) || m_callsign.empty())
  {
    cerr << "*** ERROR: " << m_name
         << "/CALLSIGN missing or empty in configuration" << endl;
    return false;
  }

  m_cfg->getValue(m_name, "AUTH_KEY", m_auth_key);
  m_cfg->getValue(m_name, "UDP_HEARTBEAT_INTERVAL",
                  m_udp_heartbeat_tx_cnt_reset);

  unsigned jitter_buffer_delay = 0;
  m_cfg->getValue(m_name, "JITTER_BUFFER_DELAY", jitter_buffer_delay);
  (void)jitter_buffer_delay;

  return true;
} /* SvxPlayer::setupConnection */


bool SvxPlayer::setupPki(void)
{
  if (!m_cfg->getValue(m_name, "CERT_PKI_DIR", m_pki_dir) ||
      m_pki_dir.empty())
  {
    m_pki_dir = string(SVX_LOCAL_STATE_DIR) + "/pki";
  }

  if (!m_pki_dir.empty() && (access(m_pki_dir.c_str(), F_OK) != 0))
  {
    cout << m_name << ": Creating PKI directory \"" << m_pki_dir << "\""
         << endl;
    if (mkdir(m_pki_dir.c_str(), 0777) != 0)
    {
      cerr << "*** ERROR: Could not create PKI directory \""
           << m_pki_dir << "\"" << endl;
      return false;
    }
  }

  if (!m_cfg->getValue(m_name, "CERT_KEYFILE", m_keyfile))
  {
    m_keyfile = m_pki_dir + "/" + m_callsign + ".key";
  }
  if (access(m_keyfile.c_str(), F_OK) != 0)
  {
    cout << m_name << ": Generating private key file \""
         << m_keyfile << "\"" << endl;
    Async::SslKeypair keypair;
    keypair.generate(2048);
    if (!keypair.writePrivateKeyFile(m_keyfile))
    {
      cerr << "*** ERROR: Failed to write private key file to \""
           << m_keyfile << "\"" << endl;
      return false;
    }
  }
  if (!m_ssl_pkey.readPrivateKeyFile(m_keyfile))
  {
    cerr << "*** ERROR: Failed to read private key file from \""
         << m_keyfile << "\"" << endl;
    return false;
  }

  m_ssl_csr.setVersion(Async::SslCertSigningReq::VERSION_1);
  m_ssl_csr.addSubjectName("CN", m_callsign);

  const vector<vector<string>> subject_names{
    {SN_givenName,              LN_givenName,               "GIVEN_NAME"},
    {SN_surname,                LN_surname,                 "SURNAME"},
    {SN_organizationalUnitName, LN_organizationalUnitName,  "ORG_UNIT"},
    {SN_organizationName,       LN_organizationName,        "ORG"},
    {SN_localityName,           LN_localityName,            "LOCALITY"},
    {SN_stateOrProvinceName,    LN_stateOrProvinceName,     "STATE"},
    {SN_countryName,            LN_countryName,             "COUNTRY"},
  };

  string value;
  const string prefix = "CERT_SUBJ_";
  for (const auto& snames : subject_names)
  {
    if (accumulate(snames.begin(), snames.end(), false,
          [&](bool found, const string& cfgsname)
          {
            return found ||
                   m_cfg->getValue(m_name, prefix + cfgsname, value);
          }) &&
        !value.empty())
    {
      if (!m_ssl_csr.addSubjectName(snames[0], value))
      {
        cerr << "*** ERROR: Failed to set subject name '"
             << snames[0] << "' in CSR" << endl;
        return false;
      }
    }
  }

  Async::SslX509Extensions csr_exts;
  csr_exts.addBasicConstraints("critical, CA:FALSE");
  csr_exts.addKeyUsage(
      "critical, digitalSignature, keyEncipherment, keyAgreement");
  csr_exts.addExtKeyUsage("clientAuth");
  vector<string> cert_email;
  if (m_cfg->getValue(m_name, "CERT_EMAIL", cert_email) &&
      !cert_email.empty())
  {
    string csr_san;
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

  if (!m_cfg->getValue(m_name, "CERT_CSRFILE", m_csrfile))
  {
    m_csrfile = m_pki_dir + "/" + m_callsign + ".csr";
  }
  Async::SslCertSigningReq req(nullptr);
  if (!req.readPemFile(m_csrfile) || !req.verify(m_ssl_pkey) ||
      (m_ssl_csr.digest() != req.digest()))
  {
    cout << m_name << ": Saving certificate signing request to '"
         << m_csrfile << "'" << endl;
    if (!m_ssl_csr.writePemFile(m_csrfile))
    {
      cerr << "*** ERROR: Failed to write CSR to '" << m_csrfile << "'"
           << endl;
      return false;
    }
  }

  if (!m_cfg->getValue(m_name, "CERT_CRTFILE", m_crtfile))
  {
    m_crtfile = m_pki_dir + "/" + m_callsign + ".crt";
  }

  if (!loadClientCertificate())
  {
    cerr << "*** WARNING[" << m_name << "]: Failed to load client certificate"
         << endl;
  }

  m_cfg->getValue(m_name, "CERT_DOWNLOAD_CA_BUNDLE", m_download_ca_bundle);
  if (!m_cfg->getValue(m_name, "CERT_CAFILE", m_cafile))
  {
    m_cafile = m_pki_dir + "/ca-bundle.crt";
  }
  if (!m_ssl_ctx.setCaCertificateFile(m_cafile))
  {
    if (m_download_ca_bundle)
    {
      cerr << "*** WARNING[" << m_name << "]: Failed to read CA file '"
           << m_cafile << "'. Will try to retrieve from server." << endl;
    }
    else
    {
      cerr << "*** ERROR[" << m_name << "]: Failed to read CA file '"
           << m_cafile << "' and CERT_DOWNLOAD_CA_BUNDLE is false." << endl;
      return false;
    }
  }

  return true;
} /* SvxPlayer::setupPki */


bool SvxPlayer::setupScheduler(void)
{
  m_scheduler.playFile.connect(
      sigc::mem_fun(*this, &SvxPlayer::playFile));
  return m_scheduler.initialize(*m_cfg, m_name, m_default_tg);
} /* SvxPlayer::setupScheduler */


bool SvxPlayer::setupPty(void)
{
  m_cfg->getValue(m_name, "PTY", m_pty_path);
  if (m_pty_path.empty())
  {
    return true;
  }

  m_pty = new Async::Pty(m_pty_path);
  if (!m_pty->open())
  {
    cerr << "*** ERROR[" << m_name << "]: Failed to open PTY at '"
         << m_pty_path << "'" << endl;
    delete m_pty;
    m_pty = nullptr;
    return false;
  }

  m_pty->dataReceived.connect(
      sigc::mem_fun(*this, &SvxPlayer::onPtyData));

  cout << m_name << ": PTY opened at '" << m_pty_path << "'" << endl;
  return true;
} /* SvxPlayer::setupPty */


void SvxPlayer::onConnected(void)
{
  cout << "NOTICE[" << m_name << "]: Connected to "
       << m_con.remoteHost() << ":" << m_con.remotePort()
       << " (" << (m_con.isPrimary() ? "primary" : "secondary") << ")"
       << endl;
  sendMsg(proto_ver);
  m_udp_heartbeat_tx_cnt = m_udp_heartbeat_tx_cnt_reset;
  m_udp_heartbeat_rx_cnt = UDP_HEARTBEAT_RX_CNT_RESET;
  m_tcp_heartbeat_tx_cnt = TCP_HEARTBEAT_TX_CNT_RESET;
  m_tcp_heartbeat_rx_cnt = TCP_HEARTBEAT_RX_CNT_RESET;
  m_heartbeat_timer.setEnable(true);
  m_next_udp_rx_seq = 0;
  timerclear(&m_last_talker_timestamp);
  m_con_state = STATE_EXPECT_CA_INFO;
} /* SvxPlayer::onConnected */


void SvxPlayer::onDisconnected(TcpConnection*,
                               TcpConnection::DisconnectReason reason)
{
  m_con_state = STATE_DISCONNECTED;
  m_playing = false;

  cout << m_name << ": Disconnected from "
       << m_con.remoteHost() << ":" << m_con.remotePort() << ": "
       << TcpConnection::disconnectReasonStr(reason) << endl;

  m_flush_timeout_timer.setEnable(false);
  m_msg_handler->clear();
  while (!m_play_queue.empty())
  {
    m_play_queue.pop();
  }

  m_reconnect_timer.setEnable(
      reason == TcpConnection::DR_ORDERED_DISCONNECT);
  delete m_udp_sock;
  m_udp_sock = nullptr;
  m_next_udp_rx_seq = 0;
  m_heartbeat_timer.setEnable(false);

  if (timerisset(&m_last_talker_timestamp))
  {
    m_dec->flushEncodedSamples();
    timerclear(&m_last_talker_timestamp);
  }
} /* SvxPlayer::onDisconnected */


bool SvxPlayer::onVerifyPeer(TcpConnection*, bool preverify_ok,
                             X509_STORE_CTX* x509_store_ctx)
{
  Async::SslX509 cert(*x509_store_ctx);
  preverify_ok = preverify_ok && !cert.isNull();
  preverify_ok = preverify_ok && !cert.commonName().empty();
  if (!preverify_ok)
  {
    cerr << "*** ERROR[" << m_name
         << "]: Certificate verification failed for reflector server" << endl;
    cout << "------------- Peer Certificate --------------" << endl;
    cert.print();
    cout << "---------------------------------------------" << endl;
  }
  return preverify_ok;
} /* SvxPlayer::onVerifyPeer */


void SvxPlayer::onSslConnectionReady(TcpConnection*)
{
  cout << m_name << ": Encrypted connection established" << endl;

  if (m_con_state != STATE_EXPECT_SSL_CON_READY)
  {
    cerr << "*** ERROR[" << m_name
         << "]: Unexpected SSL connection readiness" << endl;
    disconnect();
    return;
  }

  if (m_con.sslVerifyResult() != X509_V_OK)
  {
    cerr << "*** ERROR[" << m_name
         << "]: SSL Certificate verification failed" << endl;
    disconnect();
    return;
  }

  auto peer_cert = m_con.sslPeerCertificate();

  bool cert_match_host = false;
  string remote_name = m_con.remoteHostName();
  if (!remote_name.empty())
  {
    if (remote_name[remote_name.size() - 1] == '.')
    {
      remote_name.erase(remote_name.size() - 1);
    }
    cert_match_host |= peer_cert.matchHost(remote_name);
  }
  cert_match_host |= peer_cert.matchIp(m_con.remoteHost());
  if (!cert_match_host)
  {
    cerr << "*** ERROR[" << m_name
         << "]: Server certificate does not match hostname ("
         << remote_name << ") nor IP (" << m_con.remoteHost() << ")" << endl;
    disconnect();
    return;
  }

  m_con_state = STATE_EXPECT_AUTH_ANSWER;
} /* SvxPlayer::onSslConnectionReady */


void SvxPlayer::onFrameReceived(FramedTcpConnection*,
                                vector<uint8_t>& data)
{
  char* buf = reinterpret_cast<char*>(&data.front());
  int len = data.size();

  stringstream ss;
  ss.write(buf, len);

  ReflectorMsg header;
  if (!header.unpack(ss))
  {
    cerr << "*** ERROR[" << m_name
         << "]: Unpacking failed for TCP message header" << endl;
    disconnect();
    return;
  }

  if ((header.type() >= 100) && (m_con_state < STATE_AUTHENTICATED))
  {
    cerr << "*** ERROR[" << m_name
         << "]: Unexpected protocol message received with type="
         << header.type() << endl;
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
    case MsgAuthChallenge::TYPE:
      handleMsgAuthChallenge(ss);
      break;
    case MsgAuthOk::TYPE:
      handleMsgAuthOk();
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
    case MsgClientCsrRequest::TYPE:
      handleMsgClientCsrRequest();
      break;
    case MsgClientCert::TYPE:
      handleMsgClientCert(ss);
      break;
    case MsgServerInfo::TYPE:
      handleMsgServerInfo(ss);
      break;
    case MsgStartUdpEncryption::TYPE:
      handlMsgStartUdpEncryption(ss);
      break;
    default:
      break;
  }
} /* SvxPlayer::onFrameReceived */


void SvxPlayer::handleMsgError(istream& is)
{
  MsgError msg;
  if (!msg.unpack(is))
  {
    cerr << "*** ERROR[" << m_name
         << "]: Could not unpack MsgError" << endl;
    disconnect();
    return;
  }
  cerr << "*** ERROR[" << m_name << "]: Server error: " << msg.message()
       << endl;
  disconnect();
} /* SvxPlayer::handleMsgError */


void SvxPlayer::handleMsgProtoVerDowngrade(istream& is)
{
  MsgProtoVerDowngrade msg;
  if (!msg.unpack(is))
  {
    cerr << "*** ERROR[" << m_name
         << "]: Could not unpack MsgProtoVerDowngrade" << endl;
    disconnect();
    return;
  }
  cout << m_name
       << ": Server too old; cannot downgrade to protocol version "
       << msg.majorVer() << "." << msg.minorVer() << " from "
       << proto_ver.majorVer() << "." << proto_ver.minorVer() << endl;
  disconnect();
} /* SvxPlayer::handleMsgProtoVerDowngrade */


void SvxPlayer::handleMsgAuthChallenge(istream& is)
{
  if (m_con_state != STATE_EXPECT_AUTH_ANSWER)
  {
    cerr << "*** ERROR[" << m_name
         << "]: Unexpected MsgAuthChallenge" << endl;
    disconnect();
    return;
  }
  MsgAuthChallenge msg;
  if (!msg.unpack(is))
  {
    cerr << "*** ERROR[" << m_name
         << "]: Could not unpack MsgAuthChallenge" << endl;
    disconnect();
    return;
  }
  const uint8_t* challenge = msg.challenge();
  if (challenge == nullptr)
  {
    cerr << "*** ERROR[" << m_name
         << "]: Illegal challenge received" << endl;
    disconnect();
    return;
  }
  sendMsg(MsgAuthResponse(m_callsign, m_auth_key, challenge));
} /* SvxPlayer::handleMsgAuthChallenge */


void SvxPlayer::handleMsgAuthOk(void)
{
  if (m_con_state != STATE_EXPECT_AUTH_ANSWER)
  {
    cerr << "*** ERROR[" << m_name << "]: Unexpected MsgAuthOk" << endl;
    disconnect();
    return;
  }
  cout << m_name << ": Authentication OK" << endl;
  m_con_state = STATE_EXPECT_SERVER_INFO;

  auto cert = m_con.sslCertificate();
  if (!cert.isNull())
  {
    struct stat csrst, crtst;
    if ((stat(m_csrfile.c_str(), &csrst) == 0) &&
        (stat(m_crtfile.c_str(), &crtst) == 0) &&
        (csrst.st_mtim.tv_sec > crtst.st_mtim.tv_sec))
    {
      cout << m_name
           << ": CSR is newer than certificate; sending CSR to server" << endl;
      sendMsg(MsgClientCsr(m_ssl_csr.pem()));
    }
  }
} /* SvxPlayer::handleMsgAuthOk */


void SvxPlayer::handleMsgCAInfo(istream& is)
{
  if (m_con_state != STATE_EXPECT_CA_INFO)
  {
    cerr << "*** ERROR[" << m_name << "]: Unexpected MsgCAInfo" << endl;
    disconnect();
    return;
  }
  MsgCAInfo msg;
  if (!msg.unpack(is))
  {
    cerr << "*** ERROR[" << m_name
         << "]: Could not unpack MsgCAInfo" << endl;
    disconnect();
    return;
  }

  bool request_ca_bundle = false;
  if (m_download_ca_bundle)
  {
    ifstream ca_ifs(m_cafile);
    request_ca_bundle = !ca_ifs.good();
    if (ca_ifs.good())
    {
      string ca_pem(istreambuf_iterator<char>{ca_ifs}, {});
      auto ca_md = Async::Digest().md("sha256", ca_pem);
      request_ca_bundle = (ca_md != msg.md());
      if (request_ca_bundle)
      {
        cerr << "*** WARNING[" << m_name
             << "]: CA bundle needs updating. Contact the reflector sysop."
             << endl;
        request_ca_bundle = false;
      }
    }
    ca_ifs.close();
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
} /* SvxPlayer::handleMsgCAInfo */


void SvxPlayer::handleMsgCABundle(istream& is)
{
  if (m_con_state != STATE_EXPECT_CA_BUNDLE)
  {
    cerr << "*** ERROR[" << m_name << "]: Unexpected MsgCABundle" << endl;
    disconnect();
    return;
  }
  MsgCABundle msg;
  if (!msg.unpack(is))
  {
    cerr << "*** ERROR[" << m_name
         << "]: Could not unpack MsgCABundle" << endl;
    disconnect();
    return;
  }

  Async::SslX509 signing_cert;
  signing_cert.readPem(msg.certPem());

  if (msg.caPem().empty())
  {
    cerr << "*** ERROR[" << m_name << "]: Received empty CA bundle" << endl;
    disconnect();
    return;
  }

  Async::Digest dgst;
  auto signing_cert_pubkey = signing_cert.publicKey();
  bool signature_ok =
      dgst.signVerifyInit(MsgCABundle::MD_ALG, signing_cert_pubkey) &&
      dgst.signVerifyUpdate(msg.caPem()) &&
      dgst.signVerifyFinal(msg.signature());
  if (!signature_ok)
  {
    cerr << "*** WARNING[" << m_name
         << "]: Received CA bundle with invalid signature" << endl;
    disconnect();
    return;
  }

  if (!msg.caPem().empty())
  {
    cout << m_name << ": Writing received CA bundle to '" << m_cafile
         << "'" << endl;
    ofstream ofs(m_cafile);
    if (ofs.good())
    {
      ofs << msg.caPem();
      ofs.close();
    }
    else
    {
      cerr << "*** ERROR[" << m_name << "]: Could not write CA file '"
           << m_cafile << "'" << endl;
    }

    if (!m_ssl_ctx.setCaCertificateFile(m_cafile))
    {
      cerr << "*** ERROR[" << m_name << "]: Failed to read CA file '"
           << m_cafile << "'" << endl;
      disconnect();
      return;
    }
  }

  sendMsg(MsgStartEncryptionRequest());
  m_con_state = STATE_EXPECT_START_ENCRYPTION;
} /* SvxPlayer::handleMsgCABundle */


void SvxPlayer::handleMsgStartEncryption(void)
{
  if (m_con_state != STATE_EXPECT_START_ENCRYPTION)
  {
    cerr << "*** ERROR[" << m_name
         << "]: Unexpected MsgStartEncryption" << endl;
    disconnect();
    return;
  }
  cout << m_name << ": Setting up encrypted communications channel" << endl;
  m_con.enableSsl(true);
  m_con_state = STATE_EXPECT_SSL_CON_READY;
} /* SvxPlayer::handleMsgStartEncryption */


void SvxPlayer::handleMsgClientCsrRequest(void)
{
  if (m_con_state != STATE_EXPECT_AUTH_ANSWER)
  {
    cerr << "*** ERROR[" << m_name
         << "]: Unexpected MsgClientCsrRequest" << endl;
    disconnect();
    return;
  }
  cout << m_name << ": Sending requested Certificate Signing Request"
       << endl;
  sendMsg(MsgClientCsr(m_ssl_csr.pem()));
} /* SvxPlayer::handleMsgClientCsrRequest */


void SvxPlayer::handleMsgClientCert(istream& is)
{
  if (m_con_state < STATE_EXPECT_AUTH_ANSWER)
  {
    cerr << "*** ERROR[" << m_name << "]: Unexpected MsgClientCert" << endl;
    disconnect();
    return;
  }
  MsgClientCert msg;
  if (!msg.unpack(is))
  {
    cerr << "*** ERROR[" << m_name
         << "]: Could not unpack MsgClientCert" << endl;
    disconnect();
    return;
  }

  if (msg.certPem().empty())
  {
    cout << m_name << ": Received empty certificate" << endl;
    disconnect();
    return;
  }

  cout << m_name << ": Received certificate from server" << endl;
  Async::SslX509 cert;
  if (!cert.readPem(msg.certPem()) || cert.isNull())
  {
    cerr << "*** ERROR[" << m_name
         << "]: Failed to parse certificate PEM data from server" << endl;
    disconnect();
    return;
  }
  cout << "---------- New Client Certificate -----------" << endl;
  cert.print();
  cout << "---------------------------------------------" << endl;

  if (cert.publicKey() != m_ssl_csr.publicKey())
  {
    cerr << "*** ERROR[" << m_name
         << "]: Client certificate received does not match our private key"
         << endl;
    disconnect();
    return;
  }

  ofstream ofs(m_crtfile);
  if (!ofs.good() || !(ofs << msg.certPem()))
  {
    cerr << "*** ERROR[" << m_name
         << "]: Failed to write certificate to \"" << m_crtfile << "\""
         << endl;
    disconnect();
    return;
  }
  ofs.close();

  if (!loadClientCertificate())
  {
    cout << m_name << ": Failed to load client certificate" << endl;
    disconnect();
    return;
  }

  reconnect();
} /* SvxPlayer::handleMsgClientCert */


void SvxPlayer::handleMsgServerInfo(istream& is)
{
  if (m_con_state != STATE_EXPECT_SERVER_INFO)
  {
    cerr << "*** ERROR[" << m_name << "]: Unexpected MsgServerInfo" << endl;
    disconnect();
    return;
  }
  MsgServerInfo msg;
  if (!msg.unpack(is))
  {
    cerr << "*** ERROR[" << m_name
         << "]: Could not unpack MsgServerInfo" << endl;
    disconnect();
    return;
  }
  m_client_id = msg.clientId();

  cout << m_name << ": Connected nodes: ";
  const vector<string>& nodes = msg.nodes();
  if (!nodes.empty())
  {
    auto it = nodes.begin();
    cout << *it++;
    for (; it != nodes.end(); ++it)
    {
      cout << ", " << *it;
    }
  }
  cout << endl;

  string selected_codec;
  for (const auto& c : msg.codecs())
  {
    if (codecIsAvailable(c))
    {
      selected_codec = c;
      break;
    }
  }

  if (m_codec != "OPUS" || selected_codec.empty())
  {
    for (const auto& c : msg.codecs())
    {
      if (c == m_codec && codecIsAvailable(c))
      {
        selected_codec = c;
        break;
      }
    }
  }

  if (selected_codec.empty())
  {
    for (const auto& c : msg.codecs())
    {
      if (codecIsAvailable(c))
      {
        selected_codec = c;
        break;
      }
    }
  }

  cout << m_name << ": ";
  if (!selected_codec.empty())
  {
    cout << "Using audio codec \"" << selected_codec << "\"" << endl;
    setAudioCodec(selected_codec);
  }
  else
  {
    cout << "No supported codec found" << endl;
    disconnect();
    return;
  }

  const auto cipher = EncryptedUdpSocket::fetchCipher(UdpCipher::NAME);
  cout << m_name << ": ";
  if (cipher != nullptr)
  {
    cout << "Using UDP cipher "
         << EncryptedUdpSocket::cipherName(cipher) << endl;
  }
  else
  {
    cout << "Unsupported UDP cipher " << UdpCipher::NAME << endl;
    disconnect();
    return;
  }

  delete m_udp_sock;
  m_udp_cipher_iv_cntr = 1;
  m_udp_sock = new Async::EncryptedUdpSocket;
  m_udp_cipher_iv_rand.resize(UdpCipher::IVRANDLEN);
  const char* err = "unknown reason";
  if ((err = "memory allocation failure",     m_udp_sock == nullptr) ||
      (err = "initialization failure",        !m_udp_sock->initOk()) ||
      (err = "unsupported cipher",            !m_udp_sock->setCipher(cipher)) ||
      (err = "cipher IV rand generation failure",
       !Async::EncryptedUdpSocket::randomBytes(m_udp_cipher_iv_rand)) ||
      (err = "cipher key generation failure", !m_udp_sock->setCipherKey()))
  {
    cerr << "*** ERROR[" << m_name
         << "]: Could not create UDP socket due to " << err << endl;
    delete m_udp_sock;
    m_udp_sock = nullptr;
    disconnect();
    return;
  }
  m_udp_sock->setCipherAADLength(UdpCipher::AADLEN);
  m_udp_sock->setTagLength(UdpCipher::TAGLEN);
  m_udp_sock->cipherDataReceived.connect(
      sigc::mem_fun(*this, &SvxPlayer::udpCipherDataReceived));
  m_udp_sock->dataReceived.connect(
      sigc::mem_fun(*this, &SvxPlayer::udpDatagramReceived));

  m_con_state = STATE_EXPECT_START_UDP_ENCRYPTION;

  Json::Value node_info;
  node_info["sw"] = "SvxPlayer";
  node_info["swVer"] = SVXLINK_APP_VERSION;
  node_info["projVer"] = PROJECT_VERSION;
  struct utsname osInfo{};
  if (uname(&osInfo) == 0)
  {
    node_info["machineArch"] = osInfo.machine;
  }

  ostringstream node_info_os;
  Json::StreamWriterBuilder builder;
  builder["commentStyle"] = "None";
  builder["indentation"] = "";
  Json::StreamWriter* writer = builder.newStreamWriter();
  writer->write(node_info, &node_info_os);
  delete writer;

  MsgNodeInfo node_info_msg(m_udp_cipher_iv_rand, m_udp_sock->cipherKey(),
                            node_info_os.str());
  sendMsg(node_info_msg);
} /* SvxPlayer::handleMsgServerInfo */


void SvxPlayer::handlMsgStartUdpEncryption(istream& is)
{
  if (m_con_state != STATE_EXPECT_START_UDP_ENCRYPTION)
  {
    cerr << "*** ERROR[" << m_name
         << "]: Unexpected MsgStartUdpEncryption" << endl;
    disconnect();
    return;
  }
  MsgStartUdpEncryption msg;
  if (!msg.unpack(is))
  {
    cerr << "*** ERROR[" << m_name
         << "]: Could not unpack MsgStartUdpEncryption" << endl;
    disconnect();
    return;
  }
  m_con_state = STATE_EXPECT_UDP_HEARTBEAT;
  sendUdpRegisterMsg();
} /* SvxPlayer::handlMsgStartUdpEncryption */


void SvxPlayer::sendMsg(const ReflectorMsg& msg)
{
  if (!isConnected())
  {
    return;
  }
  if ((msg.type() >= 100) && (m_con_state < STATE_AUTHENTICATED))
  {
    cerr << "### " << m_name
         << ": Trying to send user message " << msg.type()
         << " in unauthenticated state" << endl;
    return;
  }

  m_tcp_heartbeat_tx_cnt = TCP_HEARTBEAT_TX_CNT_RESET;

  ostringstream ss;
  ReflectorMsg header(msg.type());
  if (!header.pack(ss) || !msg.pack(ss))
  {
    cerr << "*** ERROR[" << m_name
         << "]: Failed to pack reflector TCP message" << endl;
    disconnect();
    return;
  }
  if (m_con.write(ss.str().data(), ss.str().size()) == -1)
  {
    cerr << "*** ERROR[" << m_name
         << "]: Failed to write message to network connection" << endl;
    disconnect();
  }
} /* SvxPlayer::sendMsg */


void SvxPlayer::sendEncodedAudio(const void* buf, int count)
{
  if (!isLoggedIn())
  {
    return;
  }
  if (m_flush_timeout_timer.isEnabled())
  {
    m_flush_timeout_timer.setEnable(false);
  }
  //cout << m_name << ": Sending encoded audio: " << count << " bytes on TG #"
  //     << m_selected_tg << endl;
  sendUdpMsg(MsgUdpAudio(buf, count));
} /* SvxPlayer::sendEncodedAudio */


void SvxPlayer::flushEncodedAudio(void)
{
  if (!isLoggedIn())
  {
    flushTimeout();
    return;
  }
  cout << m_name << ": Flushing encoded audio on TG #" << m_selected_tg << endl;
  sendUdpMsg(MsgUdpFlushSamples());
  m_flush_timeout_timer.setEnable(true);
} /* SvxPlayer::flushEncodedAudio */


bool SvxPlayer::udpCipherDataReceived(const IpAddress& addr, uint16_t port,
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
    cerr << "*** WARNING: Unpacking AAD failed for UDP datagram from "
         << addr << ":" << port << endl;
    return true;
  }
  m_udp_sock->setCipherIV(UdpCipher::IV{m_udp_cipher_iv_rand, 0,
                                        m_aad.iv_cntr});
  return false;
} /* SvxPlayer::udpCipherDataReceived */


void SvxPlayer::udpDatagramReceived(const IpAddress& addr, uint16_t port,
                                    void* aad, void* buf, int count)
{
  if (m_con_state < STATE_EXPECT_START_UDP_ENCRYPTION)
  {
    return;
  }

  if (addr != m_con.remoteHost())
  {
    cerr << "*** WARNING[" << m_name
         << "]: UDP packet from wrong source address " << addr
         << " (expected " << m_con.remoteHost() << ")" << endl;
    return;
  }
  if (port != m_con.remotePort())
  {
    cerr << "*** WARNING[" << m_name
         << "]: UDP packet with wrong source port " << port
         << " (expected " << m_con.remotePort() << ")" << endl;
    return;
  }

  stringstream ss;
  ss.write(reinterpret_cast<const char*>(buf), count);

  ReflectorUdpMsg header;
  if (!header.unpack(ss))
  {
    cerr << "*** WARNING[" << m_name
         << "]: Unpacking failed for UDP message header" << endl;
    return;
  }

  if (m_aad.iv_cntr < m_next_udp_rx_seq)
  {
    cout << m_name << ": Dropping out-of-sequence UDP frame seq="
         << m_aad.iv_cntr << endl;
    return;
  }
  m_next_udp_rx_seq = m_aad.iv_cntr + 1;
  m_udp_heartbeat_rx_cnt = UDP_HEARTBEAT_RX_CNT_RESET;

  if ((m_con_state == STATE_EXPECT_UDP_HEARTBEAT) &&
      (header.type() == MsgUdpHeartbeat::TYPE))
  {
    cout << m_name << ": Bidirectional UDP communication verified" << endl;
    m_con.markAsEstablished();
    m_con_state = STATE_CONNECTED;

    if (m_selected_tg > 0)
    {
      cout << m_name << ": Selecting TG #" << m_selected_tg << endl;
      sendMsg(MsgSelectTG(m_selected_tg));
    }

    if (!m_play_queue.empty())
    {
      startNextPlayback();
    }
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
        cerr << "*** WARNING[" << m_name
             << "]: Could not unpack MsgUdpAudio" << endl;
        return;
      }
      if (!msg.audioData().empty())
      {
        gettimeofday(&m_last_talker_timestamp, nullptr);
        m_dec->writeEncodedSamples(
            &msg.audioData().front(),
            static_cast<int>(msg.audioData().size()));
      }
      break;
    }

    case MsgUdpFlushSamples::TYPE:
      m_dec->flushEncodedSamples();
      timerclear(&m_last_talker_timestamp);
      break;

    case MsgUdpAllSamplesFlushed::TYPE:
      cout << m_name << ": Received MsgUdpAllSamplesFlushed from reflector"
           << endl;
      m_enc->allEncodedSamplesFlushed();
      break;

    default:
      break;
  }
} /* SvxPlayer::udpDatagramReceived */


void SvxPlayer::sendUdpMsg(const UdpCipher::AAD& aad,
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
    cerr << "*** ERROR[" << m_name
         << "]: Failed to pack reflector UDP message" << endl;
    return;
  }
  m_udp_sock->setCipherIV(UdpCipher::IV{m_udp_cipher_iv_rand, m_client_id,
                                        aad.iv_cntr});
  ostringstream adss;
  if (!aad.pack(adss))
  {
    cerr << "*** WARNING: Packing AAD failed for UDP datagram" << endl;
    return;
  }
  m_udp_sock->write(m_con.remoteHost(), m_con.remotePort(),
                    adss.str().data(), adss.str().size(),
                    ss.str().data(), ss.str().size());
} /* SvxPlayer::sendUdpMsg */


void SvxPlayer::sendUdpMsg(const ReflectorUdpMsg& msg)
{
  if (!isLoggedIn())
  {
    return;
  }
  sendUdpMsg(UdpCipher::AAD{m_udp_cipher_iv_cntr++}, msg);
} /* SvxPlayer::sendUdpMsg */


void SvxPlayer::sendUdpRegisterMsg(void)
{
  sendUdpMsg(UdpCipher::InitialAAD{m_client_id}, MsgUdpHeartbeat());
} /* SvxPlayer::sendUdpRegisterMsg */


void SvxPlayer::connect(void)
{
  if (!isConnected())
  {
    m_reconnect_timer.setEnable(false);
    cout << m_name << ": Connecting to service " << m_con.service() << endl;
    m_con.connect();
    m_con.setSslContext(m_ssl_ctx, false);
  }
} /* SvxPlayer::connect */


void SvxPlayer::disconnect(void)
{
  bool was_connected = m_con.isConnected();
  m_con.disconnect();
  if (was_connected)
  {
    onDisconnected(&m_con, TcpConnection::DR_ORDERED_DISCONNECT);
  }
  m_con_state = STATE_DISCONNECTED;
} /* SvxPlayer::disconnect */


void SvxPlayer::reconnect(void)
{
  disconnect();
  connect();
} /* SvxPlayer::reconnect */


bool SvxPlayer::isConnected(void) const
{
  return m_con.isConnected();
} /* SvxPlayer::isConnected */


void SvxPlayer::allEncodedSamplesFlushed(void)
{
  cout << m_name << ": All encoded samples flushed (decoder side)" << endl;
  sendUdpMsg(MsgUdpAllSamplesFlushed());
} /* SvxPlayer::allEncodedSamplesFlushed */


void SvxPlayer::flushTimeout(Async::Timer*)
{
  m_flush_timeout_timer.setEnable(false);
  if (isLoggedIn())
  {
    m_enc->allEncodedSamplesFlushed();
  }
} /* SvxPlayer::flushTimeout */


void SvxPlayer::handleTimerTick(Async::Timer*)
{
  if (timerisset(&m_last_talker_timestamp))
  {
    struct timeval now, diff;
    gettimeofday(&now, nullptr);
    timersub(&now, &m_last_talker_timestamp, &diff);
    if (diff.tv_sec > 3)
    {
      cout << m_name << ": Last talker audio timeout" << endl;
      m_dec->flushEncodedSamples();
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
    cout << m_name << ": UDP Heartbeat timeout" << endl;
    disconnect();
  }

  if (--m_tcp_heartbeat_rx_cnt == 0)
  {
    cout << m_name << ": TCP Heartbeat timeout" << endl;
    disconnect();
  }
} /* SvxPlayer::handleTimerTick */


bool SvxPlayer::setAudioCodec(const string& codec_name)
{
  if (m_pacer != nullptr)
  {
    m_pacer->unregisterSink();
  }
  delete m_enc;
  m_enc = Async::AudioEncoder::create(codec_name);
  if (m_enc == nullptr)
  {
    cerr << "*** ERROR[" << m_name << "]: Failed to initialize "
         << codec_name << " audio encoder" << endl;
    m_enc = Async::AudioEncoder::create("DUMMY");
    assert(m_enc != nullptr);
    return false;
  }
  m_enc->writeEncodedSamples.connect(
      sigc::mem_fun(*this, &SvxPlayer::sendEncodedAudio));
  m_enc->flushEncodedSamples.connect(
      sigc::mem_fun(*this, &SvxPlayer::flushEncodedAudio));

  if (m_pacer != nullptr)
  {
    m_pacer->registerSink(m_enc, false);
  }

  string opt_prefix(m_enc->name());
  opt_prefix += "_ENC_";
  list<string> names = m_cfg->listSection(m_name);
  for (const auto& n : names)
  {
    if (n.find(opt_prefix) == 0)
    {
      string opt_value;
      m_cfg->getValue(m_name, n, opt_value);
      string opt_name(n.substr(opt_prefix.size()));
      m_enc->setOption(opt_name, opt_value);
    }
  }
  m_enc->printCodecParams();

  Async::AudioSink* sink = nullptr;
  if (m_dec != nullptr)
  {
    sink = m_dec->sink();
    m_dec->unregisterSink();
    delete m_dec;
  }
  m_dec = Async::AudioDecoder::create(codec_name);
  if (m_dec == nullptr)
  {
    cerr << "*** ERROR[" << m_name << "]: Failed to initialize "
         << codec_name << " audio decoder" << endl;
    m_dec = Async::AudioDecoder::create("DUMMY");
    assert(m_dec != nullptr);
    return false;
  }
  m_dec->allEncodedSamplesFlushed.connect(
      sigc::mem_fun(*this, &SvxPlayer::allEncodedSamplesFlushed));
  if (sink != nullptr)
  {
    m_dec->registerSink(sink, true);
  }

  opt_prefix = string(m_dec->name()) + "_DEC_";
  names = m_cfg->listSection(m_name);
  for (const auto& n : names)
  {
    if (n.find(opt_prefix) == 0)
    {
      string opt_value;
      m_cfg->getValue(m_name, n, opt_value);
      string opt_name(n.substr(opt_prefix.size()));
      m_dec->setOption(opt_name, opt_value);
    }
  }
  m_dec->printCodecParams();

  return true;
} /* SvxPlayer::setAudioCodec */


bool SvxPlayer::codecIsAvailable(const string& codec_name)
{
  return Async::AudioEncoder::isAvailable(codec_name) &&
         Async::AudioDecoder::isAvailable(codec_name);
} /* SvxPlayer::codecIsAvailable */


void SvxPlayer::selectTg(uint32_t tg)
{
  if (tg == m_selected_tg)
  {
    return;
  }
  cout << m_name << ": Selecting TG #" << tg << endl;
  sendMsg(MsgSelectTG(tg));
  m_selected_tg = tg;
} /* SvxPlayer::selectTg */


void SvxPlayer::startNextPlayback(void)
{
  if (m_play_queue.empty() || !isLoggedIn())
  {
    return;
  }

  PlayRequest req = m_play_queue.front();
  m_play_queue.pop();

  uint32_t tg = (req.tg > 0) ? req.tg : m_default_tg;
  if (tg == 0)
  {
    cerr << m_name << ": *** WARNING: No TG configured for playback of '"
         << req.file << "'. Set DEFAULT_TG in config or specify TG in play"
            " command." << endl;
  }
  selectTg(tg);

  cout << m_name << ": Playing '" << req.file
       << "' on TG #" << tg << endl;

  m_playing = true;
  m_msg_handler->playFile(req.file);
} /* SvxPlayer::startNextPlayback */


void SvxPlayer::allMsgsWritten(void)
{
  cout << m_name << ": All messages written (playback complete)" << endl;
  m_playing = false;

  if (!m_play_queue.empty())
  {
    startNextPlayback();
  }
} /* SvxPlayer::allMsgsWritten */


bool SvxPlayer::loadClientCertificate(void)
{
  if (m_ssl_cert.readPemFile(m_crtfile) &&
      !m_ssl_cert.isNull() &&
      m_ssl_cert.timeIsWithinRange())
  {
    if (!m_ssl_ctx.setCertificateFiles(m_keyfile, m_crtfile))
    {
      cerr << "*** ERROR: Failed to read and verify key ('" << m_keyfile
           << "') and certificate ('" << m_crtfile << "') files" << endl;
      return false;
    }
  }
  return true;
} /* SvxPlayer::loadClientCertificate */


void SvxPlayer::onPtyData(const void* buf, size_t len)
{
  const char* chars = reinterpret_cast<const char*>(buf);
  static string line_buf;
  for (size_t i = 0; i < len; ++i)
  {
    if (chars[i] == '\n')
    {
      processCommand(line_buf);
      line_buf.clear();
    }
    else if (chars[i] != '\r')
    {
      line_buf += chars[i];
    }
  }
} /* SvxPlayer::onPtyData */


void SvxPlayer::processCommand(const string& line)
{
  if (line.empty())
  {
    return;
  }

  istringstream iss(line);
  string cmd;
  iss >> cmd;
  transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

  if (cmd == "STOP")
  {
    stop();
    return;
  }

  if (cmd == "PLAY")
  {
    string token1, token2;
    if (!(iss >> token1))
    {
      cerr << m_name << ": PLAY command missing arguments" << endl;
      return;
    }

    if (iss >> token2)
    {
      uint32_t tg = 0;
      try
      {
        tg = static_cast<uint32_t>(stoul(token1));
      }
      catch (const exception&)
      {
        cerr << m_name << ": Invalid TG in PLAY command: " << token1 << endl;
        return;
      }
      playFile(token2, tg);
    }
    else
    {
      playFile(token1, 0);
    }
    return;
  }

  cerr << m_name << ": Unknown PTY command: " << cmd << endl;
} /* SvxPlayer::processCommand */


/*
 * This file has not been truncated
 */
