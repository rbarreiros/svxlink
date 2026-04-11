/**
@file    EchoClient.cpp
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


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <sys/time.h>
#include <cstring>
#include <cassert>
#include <sstream>
#include <iostream>
#include <algorithm>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <EchoLinkDispatcher.h>
#include <version/SVXLINK.h>
#include <config.h>
#include <common.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "EchoClient.h"


/****************************************************************************
 *
 * Namespaces to use
 *
 ****************************************************************************/

using namespace std;
using namespace Async;
using namespace EchoLink;


/****************************************************************************
 *
 * Defines
 *
 ****************************************************************************/

#define ECHOCLIENT_NAME    "EchoClient"
#define ECHOCLIENT_VER     "1.0.0"

#define LOGERROR  0
#define LOGWARN   1
#define LOGINFO   2
#define LOGDEBUG  3


/****************************************************************************
 *
 * Public member functions
 *
 ****************************************************************************/

EchoClient::EchoClient(void)
{
} /* EchoClient::EchoClient */


EchoClient::~EchoClient(void)
{
  cleanup();
} /* EchoClient::~EchoClient */


bool EchoClient::initialize(Async::Config& cfg, const std::string& section)
{
  m_section = section;
  cfg.getValue(section, "DEBUG", m_debug);

  log(LOGINFO, ECHOCLIENT_NAME " v" ECHOCLIENT_VER " starting");

    // -- EchoLink callsign ----------------------------------------------------
  if (!cfg.getValue(section, "EL_CALLSIGN", m_el_callsign) ||
      m_el_callsign.empty())
  {
    cerr << "*** ERROR[" << section << "]: EL_CALLSIGN not configured\n";
    return false;
  }
  if (m_el_callsign == "MYCALL-L")
  {
    cerr << "*** ERROR[" << section << "]: Please set EL_CALLSIGN to a real "
            "EchoLink callsign (not MYCALL-L)\n";
    return false;
  }

    // -- EchoLink password ----------------------------------------------------
  if (!cfg.getValue(section, "EL_PASSWORD", m_el_password) ||
      m_el_password.empty())
  {
    cerr << "*** ERROR[" << section << "]: EL_PASSWORD not configured\n";
    return false;
  }
  if (m_el_password == "MyPass")
  {
    cerr << "*** ERROR[" << section << "]: Please set EL_PASSWORD to a real "
            "EchoLink password\n";
    return false;
  }

    // -- Location, sysop name, description ------------------------------------
  if (!cfg.getValue(section, "EL_LOCATION", m_location) ||
      m_location.empty())
  {
    cerr << "*** ERROR[" << section << "]: EL_LOCATION not configured\n";
    return false;
  }
  if (m_location.size() > Directory::MAX_DESCRIPTION_SIZE)
  {
    cerr << "*** WARNING[" << section << "]: EL_LOCATION is too long, "
            "truncating to " << Directory::MAX_DESCRIPTION_SIZE << " chars\n";
    m_location.resize(Directory::MAX_DESCRIPTION_SIZE);
  }

  if (!cfg.getValue(section, "EL_SYSOPNAME", m_sysop_name) ||
      m_sysop_name.empty())
  {
    cerr << "*** ERROR[" << section << "]: EL_SYSOPNAME not configured\n";
    return false;
  }

  cfg.getValue(section, "EL_DESCRIPTION", m_description);

    // -- Connection limits ----------------------------------------------------
  cfg.getValue(section, "MAX_CONNECTIONS", m_max_connections);
  cfg.getValue(section, "MAX_QSOS",        m_max_qsos);

  if (m_max_qsos > m_max_connections)
  {
    cerr << "*** ERROR[" << section << "]: MAX_CONNECTIONS (" << m_max_connections
         << ") must be >= MAX_QSOS (" << m_max_qsos << ")\n";
    return false;
  }

  cfg.getValue(section, "REJECT_CONF",     m_reject_conf);
  cfg.getValue(section, "DROP_ALL_INCOMING", m_drop_all_incoming);
  cfg.getValue(section, "ALLOW_IP",        m_allow_ip);

    // -- Connection rate limiting ----------------------------------------------
  std::string cnr_str;
  if (cfg.getValue(section, "CHECK_NR_CONNECTS", cnr_str))
  {
    vector<string> parts;
    SvxLink::splitStr(parts, cnr_str, ",");
    if (parts.size() != 3)
    {
      cerr << "*** ERROR[" << section << "]: Syntax error in CHECK_NR_CONNECTS "
              "(expected: max,ttl_secs,block_mins)\n";
      return false;
    }
    m_num_con_max        = static_cast<unsigned>(atoi(parts[0].c_str()));
    m_num_con_ttl        = static_cast<time_t>(atoi(parts[1].c_str()));
    m_num_con_block_time = static_cast<time_t>(atoi(parts[2].c_str())) * 60;
  }

    // -- Access control regexes -----------------------------------------------
  if (!setupAccessControl(cfg, section))
  {
    return false;
  }

    // -- Auto-connect ---------------------------------------------------------
  cfg.getValue(section, "AUTOCON_ECHOLINK_ID", m_autocon_echolink_id);
  int autocon_secs = m_autocon_time / 1000;
  cfg.getValue(section, "AUTOCON_TIME", autocon_secs);
  m_autocon_time = 1000 * max(autocon_secs, 5);

    // -- Talk group -----------------------------------------------------------
  cfg.getValue(section, "DEFAULT_TG", m_default_tg);

    // -- EchoLink directory servers -------------------------------------------
  vector<string> servers;
  cfg.getValue(section, "EL_SERVERS", servers);
  if (servers.empty())
  {
    servers.push_back("servers.echolink.org");
  }

    // -- Optional proxy -------------------------------------------------------
  string proxy_server;
  uint16_t proxy_port = 8100;
  string proxy_password;
  cfg.getValue(section, "EL_PROXY_SERVER",   proxy_server);
  cfg.getValue(section, "EL_PROXY_PORT",     proxy_port);
  cfg.getValue(section, "EL_PROXY_PASSWORD", proxy_password);

  if (!proxy_server.empty())
  {
    m_proxy = new Proxy(proxy_server, proxy_port, m_el_callsign, proxy_password);
    m_proxy->connect();
    log(LOGINFO, "  Using EchoLink proxy: " + proxy_server);
  }

    // -- Bind address for EchoLink UDP ----------------------------------------
  IpAddress bind_addr;
  if (cfg.getValue(section, "BIND_ADDR", bind_addr) && bind_addr.isEmpty())
  {
    cerr << "*** ERROR[" << section << "]: Invalid BIND_ADDR\n";
    return false;
  }

    // -- Initialize EchoLink directory ----------------------------------------
  m_dir = new Directory(servers, m_el_callsign, m_el_password,
                        m_location, bind_addr);
  m_dir->statusChanged.connect(
      sigc::mem_fun(*this, &EchoClient::onELStatusChanged));
  m_dir->stationListUpdated.connect(
      sigc::mem_fun(*this, &EchoClient::onELStationListUpdated));
  m_dir->error.connect(
      sigc::mem_fun(*this, &EchoClient::onELError));
  {
    const string dir_target = proxy_server.empty()
        ? servers[0] : (proxy_server + " (EchoLink proxy)");
    log(LOGINFO, m_section + ": Registering with EchoLink directory via "
        + dir_target + " (registration result follows)");
  }
  m_dir->makeOnline();

    // -- EchoLink dispatcher (listens on UDP ports) ---------------------------
  Dispatcher::setBindAddr(bind_addr);
  if (Dispatcher::instance() == nullptr)
  {
    cerr << "*** ERROR[" << section << "]: Could not create EchoLink Dispatcher "
            "(UDP ports already in use?)\n";
    return false;
  }
  if (!m_drop_all_incoming)
  {
    Dispatcher::instance()->incomingConnection.connect(
        sigc::mem_fun(*this, &EchoClient::onIncomingConnection));
  }

    // -- Audio pipeline infrastructure ----------------------------------------
  m_splitter = new AudioSplitter;
  m_selector = new AudioSelector;

    // -- Connection rate-limit periodic refresh -------------------------------
  if (m_num_con_max > 0)
  {
    m_num_con_update_timer = new Timer(6000000); // 1 hour
    m_num_con_update_timer->expired.connect(
        sigc::mem_fun(*this, &EchoClient::numConUpdate));
  }

    // -- Auto-connect timer ---------------------------------------------------
  if (m_autocon_echolink_id > 0)
  {
    m_autocon_timer = new Timer(15000, Timer::TYPE_PERIODIC);
    m_autocon_timer->expired.connect(
        sigc::mem_fun(*this, &EchoClient::checkAutoCon));
  }

  log(LOGINFO, "  EL_CALLSIGN=" + m_el_callsign);
  log(LOGINFO, "  EL_LOCATION=" + m_location);
  log(LOGINFO, "  MAX_CONNECTIONS=" + to_string(m_max_connections)
      + "  MAX_QSOS=" + to_string(m_max_qsos));
  if (m_default_tg > 0)
  {
    log(LOGINFO, "  DEFAULT_TG=" + to_string(m_default_tg));
  }

    // -- Initialize ReflectorClient (must be last) ----------------------------
  return ReflectorClient::initialize(cfg, section);
} /* EchoClient::initialize */


/****************************************************************************
 *
 * Protected member functions – ReflectorClient overrides
 *
 ****************************************************************************/

void EchoClient::onConnected(void)
{
  log(LOGINFO, m_section + ": Connected to reflector");
} /* EchoClient::onConnected */


void EchoClient::onDisconnected(void)
{
  log(LOGINFO, m_section + ": Disconnected from reflector");
    // Flush any pending encoder
  if (m_enc != nullptr)
  {
    m_enc->allEncodedSamplesFlushed();
  }
} /* EchoClient::onDisconnected */


void EchoClient::onLoggedIn(void)
{
  log(LOGINFO, m_section + ": Logged in, codec=" + codec()
      + (m_default_tg ? " TG=" + to_string(m_default_tg) : ""));

  if (m_default_tg > 0)
  {
    selectTg(m_default_tg);
  }

  ReflectorClient::onLoggedIn();
} /* EchoClient::onLoggedIn */


void EchoClient::onCodecNegotiated(const std::string& negotiated_codec)
{
  log(LOGINFO, m_section + ": Codec negotiated: " + negotiated_codec);
  if (!setupAudioPipeline(negotiated_codec))
  {
    cerr << "*** ERROR[" << m_section
         << "]: Failed to build audio pipeline for codec: "
         << negotiated_codec << "\n";
  }
} /* EchoClient::onCodecNegotiated */


void EchoClient::onAudioReceived(uint32_t /*tg*/, const std::string& /*codec*/,
                                 const void* data, int len)
{
  if (m_dec != nullptr)
  {
    m_dec->writeEncodedSamples(const_cast<void*>(data), len);
  }
} /* EchoClient::onAudioReceived */


void EchoClient::onAudioFlushed(uint32_t /*tg*/)
{
  if (m_dec != nullptr)
  {
    m_dec->flushEncodedSamples();
  }
} /* EchoClient::onAudioFlushed */


void EchoClient::onAllSamplesFlushed(void)
{
  if (m_enc != nullptr)
  {
    m_enc->allEncodedSamplesFlushed();
  }
} /* EchoClient::onAllSamplesFlushed */


void EchoClient::onTalkerStart(uint32_t tg, const std::string& callsign)
{
    // TalkerStart is from the reflector (ReflectorClient), not echolib.  The
    // callsign is the reflector node; EchoLink-side PTT is correlated here
    // when any connected QSO is already in receivingAudio() (small race if UDP
    // audio starts after this TCP message).
  string msg = m_section + ": Talker " + callsign + " starts on TG#"
               + to_string(tg);
  const string el_tx = activeEchoLinkCallsigns();
  if (!el_tx.empty())
  {
    msg += " (EchoLink remote: " + el_tx + ")";
  }
  log(LOGINFO, msg);
  m_reflector_is_rx = true;
} /* EchoClient::onTalkerStart */


void EchoClient::onTalkerStop(uint32_t tg, const std::string& callsign)
{
  string msg = m_section + ": Talker " + callsign + " stops on TG#"
               + to_string(tg);
  const string el_tx = activeEchoLinkCallsigns();
  if (!el_tx.empty())
  {
    msg += " (EchoLink remote: " + el_tx + ")";
  }
  log(LOGINFO, msg);
  m_reflector_is_rx = false;
} /* EchoClient::onTalkerStop */


Json::Value EchoClient::buildNodeInfo(void) const
{
  Json::Value info;
  info["type"]        = "echolink-client";
  info["el_callsign"] = m_el_callsign;
  info["el_location"] = m_location;
  return info;
} /* EchoClient::buildNodeInfo */


/****************************************************************************
 *
 * Private member functions
 *
 ****************************************************************************/

void EchoClient::onELStatusChanged(StationData::Status status)
{
    // EchoLink StationData uses short strings: ON=ONLINE, OFF=OFFLINE, BUSY=busy
  if (status == StationData::STAT_ONLINE)
  {
    log(LOGINFO, m_section + ": EchoLink directory: registration OK — "
                  "logged in as " + m_dir->callsign()
                  + " (EchoLink reports status \"ON\" for online)");
  }
  else if (status == StationData::STAT_BUSY)
  {
    log(LOGINFO, m_section + ": EchoLink directory: registration OK — BUSY");
  }
  else
  {
    log(LOGINFO, m_section + ": EchoLink directory status: "
        + StationData::statusStr(status));
  }

  if ((status == StationData::STAT_ONLINE) ||
      (status == StationData::STAT_BUSY))
  {
    if (m_dir_refresh_timer == nullptr)
    {
      getDirectoryList();
    }
  }
  else
  {
    delete m_dir_refresh_timer;
    m_dir_refresh_timer = nullptr;
  }
} /* EchoClient::onELStatusChanged */


void EchoClient::onELStationListUpdated(void)
{
    // Directory uses uppercase callsign; bulk list often omits your own node.
  const StationData* self_stn = m_dir->findCall(m_dir->callsign());
  if (self_stn != nullptr)
  {
    log(LOGINFO, m_section + ": EchoLink directory list includes this node: "
        + self_stn->callsign() + " at " + self_stn->ip().toString()
        + " (id " + to_string(self_stn->id()) + ")");
  }
  else if (m_dir->status() == StationData::STAT_ONLINE ||
           m_dir->status() == StationData::STAT_BUSY)
  {
    log(LOGINFO, m_section + ": EchoLink directory list does not contain "
        + m_dir->callsign()
        + " — this is normal: many clients omit your own station from the "
          "downloaded list. Others should still see you; confirm from another "
          "callsign or https://www.echolink.org/ (Links tab for *-L). "
          "UDP 5198-5199 must reach this host for stations to connect.");
  }

  if (!m_dir->message().empty())
  {
    log(LOGINFO, m_section + ": EchoLink directory server notice: "
        + m_dir->message());
  }

  if (m_pending_connect_id > 0)
  {
    const StationData* stn = m_dir->findStation(m_pending_connect_id);
    if (stn != nullptr)
    {
      createOutgoingConnection(*stn);
    }
    else
    {
      cerr << "*** WARNING[" << m_section << "]: EchoLink ID "
           << m_pending_connect_id << " not found in directory\n";
    }
    m_pending_connect_id = -1;
  }
} /* EchoClient::onELStationListUpdated */


void EchoClient::onELError(const std::string& msg)
{
  cerr << "*** ERROR[" << m_section << "]: EchoLink directory: " << msg << "\n";
} /* EchoClient::onELError */


void EchoClient::onIncomingConnection(const IpAddress& ip,
                                      const string& callsign,
                                      const string& name,
                                      const string& priv)
{
  log(LOGINFO, "Incoming EchoLink from " + callsign
      + " (" + name + ") at " + ip.toString());

    // Silent drop based on regex
  if (regexec(m_drop_incoming_regex, callsign.c_str(), 0, nullptr, 0) == 0)
  {
    log(LOGWARN, "Dropping incoming from " + callsign + " (DROP_INCOMING)");
    return;
  }

    // Too many connections
  if (m_qsos.size() >= m_max_connections)
  {
    log(LOGWARN, "Ignoring incoming from " + callsign
        + " (too many connections: " + to_string(m_qsos.size()) + ")");
    return;
  }

    // Validate against directory (unless ALLOW_IP matches)
  const StationData* station = nullptr;
  StationData tmp_stn;
  if (ip.isWithinSubet(m_allow_ip))
  {
    tmp_stn.setIp(ip);
    tmp_stn.setCallsign(callsign);
    station = &tmp_stn;
  }
  else
  {
    station = m_dir->findCall(callsign);
    if (station == nullptr)
    {
      log(LOGWARN, "Callsign " + callsign + " not in directory, refreshing");
      getDirectoryList();
      return;
    }
    if (station->ip() != ip)
    {
      cerr << "*** WARNING[" << m_section << "]: Ignoring incoming from "
           << callsign << ": IP mismatch (dir=" << station->ip()
           << " actual=" << ip << ")\n";
      getDirectoryList();
      return;
    }
  }

    // Rate-limit check (before creating the QSO object)
  if ((m_num_con_max > 0) && !numConCheck(callsign))
  {
    log(LOGWARN, "Dropping " + callsign + " (rate limit exceeded)");
    return;
  }

    // Too many simultaneous QSOs
  if (m_qsos.size() >= m_max_qsos)
  {
    log(LOGWARN, "Dropping " + callsign + " (too many active QSOs)");
    return;
  }

    // Callsign-based accept/reject
  if ((regexec(m_reject_incoming_regex, callsign.c_str(), 0, nullptr, 0) == 0) ||
      (regexec(m_accept_incoming_regex, callsign.c_str(), 0, nullptr, 0) != 0))
  {
    log(LOGWARN, "Rejecting " + callsign + " (access control)");
    return;
  }

  if (m_reject_conf &&
      name.size() > 3 && name.rfind("CONF") == (name.size() - 4))
  {
    log(LOGWARN, "Rejecting conference node " + callsign);
    return;
  }

    // Create QSO and accept
  EchoLink::Qso* qso = createQso(ip, callsign, name, priv);
  if (qso == nullptr)
  {
    return;
  }
  qso->accept();
  broadcastTalkerStatus();
  updateDescription();

  log(LOGINFO, "Accepted incoming EchoLink from " + callsign);
} /* EchoClient::onIncomingConnection */


void EchoClient::onQsoStateChange(EchoLink::Qso::State state, EchoLink::Qso* qso)
{
  switch (state)
  {
    case EchoLink::Qso::STATE_DISCONNECTED:
      log(LOGINFO, "QSO disconnected: " + qso->remoteCallsign());

      if (!qso->remoteCallsign().empty())
      {
        m_last_disc_station.setCallsign(qso->remoteCallsign());
      }

      if (m_talker == qso)
      {
        m_talker = findFirstTalker();
      }

      if (m_autocon_timer != nullptr)
      {
        m_autocon_timer->setTimeout(m_autocon_time);
      }

      broadcastTalkerStatus();
      updateDescription();
      destroyQso(qso);
      break;

    case EchoLink::Qso::STATE_CONNECTED:
      log(LOGINFO, "QSO connected: " + qso->remoteCallsign());
      broadcastTalkerStatus();
      updateDescription();
      break;

    default:
      break;
  }
} /* EchoClient::onQsoStateChange */


void EchoClient::onQsoIsReceiving(bool is_receiving, EchoLink::Qso* qso)
{
  log(LOGDEBUG, qso->remoteCallsign() + ": isReceiving="
      + (is_receiving ? "true" : "false"));

    // If remote EchoLink station starts transmitting and we're not receiving
    // from the reflector, elect this QSO as the active talker
  if (is_receiving)
  {
    if (m_reject_conf)
    {
      const string& rname = qso->remoteName();
      if (rname.size() > 3 && rname.rfind("CONF") == (rname.size() - 4))
      {
        qso->sendChatData("Conference connections are not allowed");
        qso->disconnect();
        return;
      }
    }
    if (m_talker == nullptr)
    {
      m_talker = qso;
      broadcastTalkerStatus();
    }
  }
  else if (m_talker == qso)
  {
    m_talker = findFirstTalker();
    if (m_talker != nullptr)
    {
      broadcastTalkerStatus();
    }
    else
    {
      broadcastTalkerStatus();
    }
  }
} /* EchoClient::onQsoIsReceiving */


void EchoClient::onQsoAudioReceivedRaw(EchoLink::Qso::RawPacket* packet,
                                       EchoLink::Qso* qso)
{
    // Forward raw GSM audio between connected EchoLink stations
    // (act as a local conference when > 1 QSO is active)
  if (m_reflector_is_rx)
  {
    return;
  }

  if (qso == m_talker)
  {
    for (auto* other : m_qsos)
    {
      if (other != qso &&
          other->currentState() == EchoLink::Qso::STATE_CONNECTED)
      {
        other->sendAudioRaw(packet);
      }
    }
  }
} /* EchoClient::onQsoAudioReceivedRaw */


void EchoClient::onQsoChatMsgReceived(const std::string& msg,
                                      EchoLink::Qso* /*qso*/)
{
  log(LOGINFO, "EchoLink chat: " + msg);
    // Broadcast the chat to all other connected EchoLink stations
  for (auto* q : m_qsos)
  {
    q->sendChatData(msg);
  }
} /* EchoClient::onQsoChatMsgReceived */


void EchoClient::onQsoInfoMsgReceived(const std::string& msg,
                                      EchoLink::Qso* qso)
{
    // Remote apps often re-send identical INFO (NDATA) packets on a timer.
  auto it = m_last_qso_info_msg.find(qso);
  if (it != m_last_qso_info_msg.end() && it->second == msg)
  {
    return;
  }
  m_last_qso_info_msg[qso] = msg;

  log(LOGDEBUG, "EchoLink info from " + qso->remoteCallsign() + ": " + msg);
} /* EchoClient::onQsoInfoMsgReceived */


void EchoClient::getDirectoryList(Async::Timer* /*t*/)
{
  delete m_dir_refresh_timer;
  m_dir_refresh_timer = nullptr;

  if ((m_dir->status() == StationData::STAT_ONLINE) ||
      (m_dir->status() == StationData::STAT_BUSY))
  {
    m_dir->getCalls();
    m_dir_refresh_timer = new Timer(600000); // 10 minutes
    m_dir_refresh_timer->expired.connect(
        sigc::mem_fun(*this, &EchoClient::getDirectoryList));
  }
} /* EchoClient::getDirectoryList */


EchoLink::Qso* EchoClient::createQso(const IpAddress& ip,
                                     const string& remote_callsign,
                                     const string& remote_name,
                                     const string& remote_priv)
{
  EchoLink::Qso* qso = new EchoLink::Qso(ip, m_el_callsign, m_sysop_name,
                                          m_description);
  if (!qso->initOk())
  {
    cerr << "*** ERROR[" << m_section << "]: Failed to create Qso for "
         << remote_callsign << "\n";
    delete qso;
    return nullptr;
  }

  qso->setRemoteCallsign(remote_callsign);
  if (!remote_name.empty())  qso->setRemoteName(remote_name);
  if (!remote_priv.empty())  qso->setRemoteParams(remote_priv);

    // Connect per-QSO signals (use sigc::bind to pass qso pointer)
  qso->stateChange.connect(
      sigc::bind(sigc::mem_fun(*this, &EchoClient::onQsoStateChange), qso));
  qso->isReceiving.connect(
      sigc::bind(sigc::mem_fun(*this, &EchoClient::onQsoIsReceiving), qso));
  qso->audioReceivedRaw.connect(
      sigc::bind(sigc::mem_fun(*this, &EchoClient::onQsoAudioReceivedRaw), qso));
  qso->chatMsgReceived.connect(
      sigc::bind(sigc::mem_fun(*this, &EchoClient::onQsoChatMsgReceived), qso));
  qso->infoMsgReceived.connect(
      sigc::bind(sigc::mem_fun(*this, &EchoClient::onQsoInfoMsgReceived), qso));

    // Wire into audio pipeline
  if (m_splitter != nullptr) m_splitter->addSink(qso, false);
  if (m_selector != nullptr)
  {
    m_selector->addSource(qso);
    m_selector->enableAutoSelect(qso, 0);
  }

  m_qsos.push_back(qso);
  return qso;
} /* EchoClient::createQso */


void EchoClient::createOutgoingConnection(const StationData& station)
{
  if (station.callsign() == m_el_callsign)
  {
    log(LOGWARN, "Cannot connect to self (" + m_el_callsign + ")");
    return;
  }

  if ((regexec(m_reject_outgoing_regex, station.callsign().c_str(),
               0, nullptr, 0) == 0) ||
      (regexec(m_accept_outgoing_regex, station.callsign().c_str(),
               0, nullptr, 0) != 0))
  {
    log(LOGWARN, "Outgoing connection to " + station.callsign()
        + " rejected by access control");
    return;
  }

  if (static_cast<unsigned>(numConnectedStations()) >= m_max_qsos)
  {
    log(LOGWARN, "Cannot connect to " + station.callsign()
        + ": max QSOs reached");
    return;
  }

    // Check if already connected
  for (auto* q : m_qsos)
  {
    if (q->remoteCallsign() == station.callsign() &&
        q->currentState() != EchoLink::Qso::STATE_DISCONNECTED)
    {
      log(LOGWARN, "Already connected to " + station.callsign());
      return;
    }
  }

  log(LOGINFO, "Connecting to " + station.callsign()
      + " (ID=" + to_string(station.id()) + ")");

  EchoLink::Qso* qso = createQso(station.ip(), station.callsign());
  if (qso == nullptr)
  {
    return;
  }
  qso->connect();
  updateDescription();
  broadcastTalkerStatus();
} /* EchoClient::createOutgoingConnection */


void EchoClient::connectByNodeId(int node_id)
{
  if ((m_dir->status() == StationData::STAT_OFFLINE) ||
      (m_dir->status() == StationData::STAT_UNKNOWN))
  {
    cerr << "*** ERROR[" << m_section << "]: Directory offline, cannot connect "
            "to node " << node_id << "\n";
    return;
  }

  const StationData* stn = m_dir->findStation(node_id);
  if (stn != nullptr)
  {
    createOutgoingConnection(*stn);
  }
  else
  {
    log(LOGINFO, "Node ID " + to_string(node_id)
        + " not in list, refreshing directory");
    getDirectoryList();
    m_pending_connect_id = node_id;
  }
} /* EchoClient::connectByNodeId */


void EchoClient::destroyQso(EchoLink::Qso* qso)
{
  m_last_qso_info_msg.erase(qso);

  if (m_splitter != nullptr) m_splitter->removeSink(qso);
  if (m_selector != nullptr) m_selector->removeSource(qso);

  auto it = find(m_qsos.begin(), m_qsos.end(), qso);
  if (it != m_qsos.end())
  {
    m_qsos.erase(it);
  }

  if (m_talker == qso)
  {
    m_talker = findFirstTalker();
  }

  delete qso;
} /* EchoClient::destroyQso */


EchoLink::Qso* EchoClient::findFirstTalker(void) const
{
  for (auto* q : m_qsos)
  {
    if (q->receivingAudio())
    {
      return q;
    }
  }
  return nullptr;
} /* EchoClient::findFirstTalker */


string EchoClient::activeEchoLinkCallsigns(void) const
{
  string out;
  for (auto* q : m_qsos)
  {
    if (q->currentState() == EchoLink::Qso::STATE_DISCONNECTED)
    {
      continue;
    }
    if (!q->receivingAudio())
    {
      continue;
    }
    if (!out.empty())
    {
      out += ", ";
    }
    out += q->remoteCallsign();
  }
  return out;
} /* EchoClient::activeEchoLinkCallsigns */


int EchoClient::numConnectedStations(void) const
{
  int cnt = 0;
  for (auto* q : m_qsos)
  {
    if (q->currentState() != EchoLink::Qso::STATE_DISCONNECTED)
    {
      ++cnt;
    }
  }
  return cnt;
} /* EchoClient::numConnectedStations */


void EchoClient::broadcastTalkerStatus(void)
{
  if (m_max_qsos < 2)
  {
    return;
  }

  ostringstream msg;
  msg << "SvxLink " << SVXLINK_APP_VERSION << " - " << m_el_callsign
      << " (" << numConnectedStations() << ")\n\n";

  if (m_reflector_is_rx)
  {
    msg << "> " << callsign() << "         " << m_sysop_name << "\n\n";
  }
  else if (m_talker != nullptr)
  {
    msg << "> " << m_talker->remoteCallsign() << "         "
        << m_talker->remoteName() << "\n\n";
    msg << m_el_callsign << "         " << m_sysop_name << "\n";
  }
  else
  {
    msg << m_el_callsign << "         " << m_sysop_name << "\n";
  }

  for (auto* q : m_qsos)
  {
    if (q->currentState() == EchoLink::Qso::STATE_DISCONNECTED)
    {
      continue;
    }
    if (q != m_talker || m_reflector_is_rx)
    {
      msg << q->remoteCallsign() << "         " << q->remoteName() << "\n";
    }
  }

  const string info_str = msg.str();
  for (auto* q : m_qsos)
  {
    q->sendInfoData(info_str);
  }
} /* EchoClient::broadcastTalkerStatus */


void EchoClient::updateDescription(void)
{
  if (m_max_qsos < 2 || m_dir == nullptr)
  {
    return;
  }

  string desc(m_location);
  int cnt = numConnectedStations();
  if (cnt > 0)
  {
    ostringstream sstr;
    sstr << " (" << cnt << ")";
    desc.resize(Directory::MAX_DESCRIPTION_SIZE - sstr.str().size(), ' ');
    desc += sstr.str();
  }
  m_dir->setDescription(desc);
  m_dir->refreshRegistration();
} /* EchoClient::updateDescription */


bool EchoClient::setupAudioPipeline(const std::string& negotiated_codec)
{
    // -- RX: Reflector → decoder → float PCM → splitter → each Qso ----------
  delete m_dec;
  m_dec = nullptr;

  m_dec = AudioDecoder::create(negotiated_codec);
  if (m_dec == nullptr)
  {
    cerr << "*** ERROR[" << m_section
         << "]: Cannot create decoder for codec: " << negotiated_codec << "\n";
    return false;
  }

    // Decoder output → splitter (splitter distributes to each QSO's AudioSink)
  m_dec->registerSink(m_splitter, false);

    // -- TX: selector → encoder → Reflector -----------------------------------
    // Disconnect previous encoder from selector (if any)
  if (m_enc != nullptr)
  {
    m_selector->unregisterSink();
    delete m_enc;
    m_enc = nullptr;
  }

  m_enc = AudioEncoder::create(negotiated_codec);
  if (m_enc == nullptr)
  {
    cerr << "*** ERROR[" << m_section
         << "]: Cannot create encoder for codec: " << negotiated_codec << "\n";
    return false;
  }

    // Encoder outputs → reflector
  m_enc->writeEncodedSamples.connect(
      sigc::mem_fun(*this,
                    (void(EchoClient::*)(const void*, int))
                    &ReflectorClient::sendEncodedAudio));
  m_enc->flushEncodedSamples.connect(
      sigc::mem_fun(*this, &ReflectorClient::flushEncodedAudio));

    // Selector (picks active EchoLink talker) feeds the encoder
  m_selector->registerSink(m_enc, false);

  log(LOGINFO, m_section + ": Audio pipeline ready (codec=" + negotiated_codec + ")");
  return true;
} /* EchoClient::setupAudioPipeline */


void EchoClient::teardownAudioPipeline(void)
{
  if (m_dec != nullptr)
  {
      // Disconnect from m_splitter; registerSink(0) hits assert in AudioSource.
    m_dec->unregisterSink();
    delete m_dec;
    m_dec = nullptr;
  }
  if (m_enc != nullptr)
  {
    if (m_selector != nullptr)
    {
      m_selector->unregisterSink();
    }
    delete m_enc;
    m_enc = nullptr;
  }
} /* EchoClient::teardownAudioPipeline */


bool EchoClient::setupAccessControl(Async::Config& cfg,
                                    const std::string& section)
{
  auto load = [&](regex_t*& re, const char* key, const char* dflt) -> bool {
    string pattern;
    if (!cfg.getValue(section, key, pattern))
    {
      pattern = dflt;
    }
    re = new regex_t;
    int err = regcomp(re, pattern.c_str(),
                      REG_EXTENDED | REG_NOSUB | REG_ICASE);
    if (err != 0)
    {
      size_t sz = regerror(err, re, nullptr, 0);
      vector<char> buf(sz);
      regerror(err, re, buf.data(), sz);
      cerr << "*** ERROR[" << section << "]: Regex " << key
           << ": " << buf.data() << "\n";
      return false;
    }
    return true;
  };

  return load(m_drop_incoming_regex,   "DROP_INCOMING",   "^$")
      && load(m_reject_incoming_regex, "REJECT_INCOMING", "^$")
      && load(m_accept_incoming_regex, "ACCEPT_INCOMING", "^.*$")
      && load(m_reject_outgoing_regex, "REJECT_OUTGOING", "^$")
      && load(m_accept_outgoing_regex, "ACCEPT_OUTGOING", "^.*$");
} /* EchoClient::setupAccessControl */


bool EchoClient::numConCheck(const std::string& callsign)
{
  struct timeval now;
  gettimeofday(&now, nullptr);

  numConUpdate();

  auto it = m_num_con_map.find(callsign);
  if (it != m_num_con_map.end())
  {
    NumConStn& stn = it->second;
    struct timeval diff;
    timersub(&now, &stn.last_con, &diff);
    if (diff.tv_sec > 3)
    {
      ++stn.num_con;
      stn.last_con = now;
    }
    if (stn.num_con > m_num_con_max)
    {
      time_t next = now.tv_sec + m_num_con_block_time;
      char tbuf[64];
      struct tm tm;
      strftime(tbuf, sizeof(tbuf), "%c", localtime_r(&next, &tm));
      cerr << "*** WARNING[" << m_section << "]: " << callsign
           << " connected too often (" << stn.num_con << " times). "
              "Blocked until " << tbuf << "\n";
      return false;
    }
  }
  else
  {
    m_num_con_map.emplace(callsign, NumConStn(1, now));
  }
  return true;
} /* EchoClient::numConCheck */


void EchoClient::numConUpdate(Async::Timer* /*t*/)
{
  struct timeval now;
  gettimeofday(&now, nullptr);

  auto it = m_num_con_map.begin();
  while (it != m_num_con_map.end())
  {
    const NumConStn& stn = it->second;
    struct timeval remove_at = stn.last_con;
    remove_at.tv_sec += (stn.num_con > m_num_con_max)
                        ? m_num_con_block_time : m_num_con_ttl;
    if (timercmp(&remove_at, &now, <))
    {
      it = m_num_con_map.erase(it);
    }
    else
    {
      ++it;
    }
  }

  if (m_num_con_update_timer != nullptr)
  {
    m_num_con_update_timer->reset();
  }
} /* EchoClient::numConUpdate */


void EchoClient::checkAutoCon(Async::Timer* /*t*/)
{
  if ((m_dir->status() == StationData::STAT_ONLINE) &&
      (numConnectedStations() == 0))
  {
    log(LOGINFO, "Auto-connecting to node ID " +
                 to_string(m_autocon_echolink_id));
    connectByNodeId(m_autocon_echolink_id);
  }
} /* EchoClient::checkAutoCon */


void EchoClient::cleanup(void)
{
  teardownAudioPipeline();

    // Disconnect all QSOs
  vector<EchoLink::Qso*> qsos_copy(m_qsos);
  for (auto* q : qsos_copy)
  {
    if (q->currentState() != EchoLink::Qso::STATE_DISCONNECTED)
    {
      q->disconnect();
    }
  }
  for (auto* q : qsos_copy)
  {
    destroyQso(q);
  }

  delete m_autocon_timer;
  m_autocon_timer = nullptr;

  delete m_num_con_update_timer;
  m_num_con_update_timer = nullptr;

  delete m_dir_refresh_timer;
  m_dir_refresh_timer = nullptr;

  Dispatcher::deleteInstance();

  delete m_dir;
  m_dir = nullptr;

  delete m_proxy;
  m_proxy = nullptr;

  delete m_splitter;
  m_splitter = nullptr;

  delete m_selector;
  m_selector = nullptr;

    // Free access control regexes
  auto free_re = [](regex_t*& re) {
    if (re != nullptr)
    {
      regfree(re);
      delete re;
      re = nullptr;
    }
  };
  free_re(m_drop_incoming_regex);
  free_re(m_reject_incoming_regex);
  free_re(m_accept_incoming_regex);
  free_re(m_reject_outgoing_regex);
  free_re(m_accept_outgoing_regex);
} /* EchoClient::cleanup */


void EchoClient::log(int level, const std::string& msg) const
{
    // ERROR / WARN / INFO always go to stdout so operators see reflector and
    // EchoLink directory state without setting DEBUG.  DEBUG lines only if
    // DEBUG>=3 (same idea as ParrotClient).
  if (level < LOGDEBUG)
  {
    cout << msg << "\n";
    return;
  }
  if (m_debug >= LOGDEBUG)
  {
    cout << msg << "\n";
  }
} /* EchoClient::log */


/*
 * This file has not been truncated
 */
