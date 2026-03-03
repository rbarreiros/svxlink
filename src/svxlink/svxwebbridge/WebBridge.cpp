/**
@file    WebBridge.cpp
@brief   Bridge between an SvxReflector and WebSocket clients
@author  Ricardo Barreiros / CT7ALW
*/


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <iostream>
#include <sstream>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncConfig.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "WebBridge.h"
#include "WsServer.h"


/****************************************************************************
 *
 * Namespaces
 *
 ****************************************************************************/

using namespace std;
using namespace Async;


/****************************************************************************
 *
 * Public member functions
 *
 ****************************************************************************/

WebBridge::WebBridge(void)
{
  m_ws_server.clientSubscribed.connect(
      sigc::mem_fun(*this, &WebBridge::onWsClientSubscribed));
  m_ws_server.clientUnsubscribed.connect(
      sigc::mem_fun(*this, &WebBridge::onWsClientUnsubscribed));
} /* WebBridge::WebBridge */


WebBridge::~WebBridge(void) {}


bool WebBridge::initialize(Async::Config& cfg, const std::string& section)
{
  m_section = section;

  uint16_t ws_port = 8080;
  cfg.getValue(section, "WS_PORT", ws_port);

  if (!m_ws_server.initialize(ws_port))
  {
    return false;
  }

    // Load static monitored TGs (those always monitored regardless of web clients)
  std::vector<std::string> monitor_tg_strs;
  cfg.getValue(section, "MONITOR_TGS", monitor_tg_strs);
  for (const auto& tg_str : monitor_tg_strs)
  {
    std::istringstream iss(tg_str);
    uint32_t tg = 0;
    if (iss >> tg && tg > 0)
    {
      m_static_monitor_tgs.insert(tg);
    }
  }

    // Initialize the reflector client base class
  return ReflectorClient::initialize(cfg, section);
} /* WebBridge::initialize */


/****************************************************************************
 *
 * Protected member functions
 *
 ****************************************************************************/

void WebBridge::onLoggedIn(void)
{
    // Send the full static + dynamically accumulated monitor set
  updateMonitorTgs();

  std::cout << m_section << ": WebBridge logged in, codec=" << codec()
            << std::endl;
} /* WebBridge::onLoggedIn */


void WebBridge::onDisconnected(void)
{
  m_active_talkers.clear();

  Json::Value msg;
  msg["type"] = "disconnected";
  m_ws_server.broadcastJson(msg);
} /* WebBridge::onDisconnected */


void WebBridge::onAudioReceived(uint32_t tg, const std::string& codec_name,
                                const void* data, int len)
{
  m_ws_server.sendAudio(tg, codec_name, data, len);
} /* WebBridge::onAudioReceived */


void WebBridge::onAudioFlushed(uint32_t tg)
{
  m_ws_server.sendFlush(tg);
} /* WebBridge::onAudioFlushed */


void WebBridge::onTalkerStart(uint32_t tg, const std::string& callsign)
{
  m_active_talkers[tg] = callsign;

  Json::Value msg;
  msg["type"]     = "talker_start";
  msg["tg"]       = tg;
  msg["callsign"] = callsign;
  m_ws_server.broadcastJson(msg);

    // Also tell clients not subscribed to this TG so they can show activity
    // indicators for other TGs in the list.
  broadcastTgList();
} /* WebBridge::onTalkerStart */


void WebBridge::onTalkerStop(uint32_t tg, const std::string& callsign)
{
  m_active_talkers.erase(tg);

  Json::Value msg;
  msg["type"]     = "talker_stop";
  msg["tg"]       = tg;
  msg["callsign"] = callsign;
  m_ws_server.broadcastJson(msg);

  broadcastTgList();
} /* WebBridge::onTalkerStop */


void WebBridge::onNodeJoined(const std::string& callsign)
{
  Json::Value msg;
  msg["type"]     = "node_joined";
  msg["callsign"] = callsign;
  m_ws_server.broadcastJson(msg);
} /* WebBridge::onNodeJoined */


void WebBridge::onNodeLeft(const std::string& callsign)
{
  Json::Value msg;
  msg["type"]     = "node_left";
  msg["callsign"] = callsign;
  m_ws_server.broadcastJson(msg);
} /* WebBridge::onNodeLeft */


void WebBridge::onQsyRequest(uint32_t tg)
{
  Json::Value msg;
  msg["type"] = "qsy";
  msg["tg"]   = tg;
  m_ws_server.broadcastJson(msg);
} /* WebBridge::onQsyRequest */


Json::Value WebBridge::buildNodeInfo(void) const
{
  Json::Value info;
  info["type"] = "web-bridge";
  return info;
} /* WebBridge::buildNodeInfo */


/****************************************************************************
 *
 * Private member functions
 *
 ****************************************************************************/

void WebBridge::onWsClientSubscribed(WsClient* client, uint32_t tg)
{
  std::cout << m_section << ": WebSocket client subscribed to TG #"
            << tg << std::endl;

    // Maintain ref-count of web subscribers per TG.
    // When a client changes TG (subscribe replaces old subscribe), decrement
    // the old TG first.  The old_tg is already cleared from the client before
    // this signal fires if it was an explicit re-subscribe; for simplicity we
    // increment first, then let updateMonitorTgs rebuild the full set.
  m_tg_refcounts[tg]++;
  updateMonitorTgs();

    // Send current state to the newly-subscribed client
  sendTgListTo(client);
  sendCodecInfoTo(client, tg);

    // If there is already an active talker on this TG, tell the client
  auto it = m_active_talkers.find(tg);
  if (it != m_active_talkers.end())
  {
    Json::Value msg;
    msg["type"]     = "talker_start";
    msg["tg"]       = tg;
    msg["callsign"] = it->second;
    client->sendJson(msg);
  }

  Json::Value ack;
  ack["type"]        = "subscribed";
  ack["tg"]          = tg;
  ack["codec"]       = codec();
  ack["sample_rate"] = 16000;
  ack["channels"]    = 1;
  client->sendJson(ack);
} /* WebBridge::onWsClientSubscribed */


void WebBridge::onWsClientUnsubscribed(WsClient* client)
{
  uint32_t tg = client->subscribedTg();
  std::cout << m_section << ": WebSocket client unsubscribed from TG #"
            << tg << std::endl;

  if (tg > 0)
  {
    auto it = m_tg_refcounts.find(tg);
    if (it != m_tg_refcounts.end())
    {
      if (--(it->second) <= 0)
      {
        m_tg_refcounts.erase(it);
      }
    }
    updateMonitorTgs();
  }
} /* WebBridge::onWsClientUnsubscribed */


void WebBridge::updateMonitorTgs(void)
{
    // Monitor set = union of the static (config-defined) TGs and all TGs that
    // currently have at least one web subscriber.
  std::set<uint32_t> combined = m_static_monitor_tgs;
  for (const auto& kv : m_tg_refcounts)
  {
    combined.insert(kv.first);
  }
  monitorTgs(combined);
} /* WebBridge::updateMonitorTgs */


void WebBridge::broadcastTgList(void)
{
  std::set<uint32_t> all_tgs = m_static_monitor_tgs;
  for (const auto& kv : m_tg_refcounts)
  {
    all_tgs.insert(kv.first);
  }

  Json::Value msg;
  msg["type"] = "tg_list";
  Json::Value tgs(Json::arrayValue);
  for (uint32_t tg : all_tgs)
  {
    Json::Value tg_obj;
    tg_obj["id"]     = tg;
    tg_obj["active"] = (m_active_talkers.count(tg) > 0);
    if (m_active_talkers.count(tg) > 0)
    {
      tg_obj["callsign"] = m_active_talkers.at(tg);
    }
    tgs.append(tg_obj);
  }
  msg["tgs"] = tgs;
  m_ws_server.broadcastJson(msg);
} /* WebBridge::broadcastTgList */


void WebBridge::sendTgListTo(WsClient* client)
{
    // Full set of monitored TGs = static ∪ dynamic
  std::set<uint32_t> all_tgs = m_static_monitor_tgs;
  for (const auto& kv : m_tg_refcounts)
  {
    all_tgs.insert(kv.first);
  }

  Json::Value msg;
  msg["type"] = "tg_list";
  Json::Value tgs(Json::arrayValue);
  for (uint32_t tg : all_tgs)
  {
    Json::Value tg_obj;
    tg_obj["id"]     = tg;
    tg_obj["active"] = (m_active_talkers.count(tg) > 0);
    if (m_active_talkers.count(tg) > 0)
    {
      tg_obj["callsign"] = m_active_talkers.at(tg);
    }
    tgs.append(tg_obj);
  }
  msg["tgs"] = tgs;
  client->sendJson(msg);
} /* WebBridge::sendTgListTo */


void WebBridge::sendCodecInfoTo(WsClient* client, uint32_t tg)
{
  Json::Value msg;
  msg["type"]        = "codec";
  msg["tg"]          = tg;
  msg["name"]        = codec();
  msg["sample_rate"] = 16000;
  msg["channels"]    = 1;
  client->sendJson(msg);
} /* WebBridge::sendCodecInfoTo */


/*
 * This file has not been truncated
 */
