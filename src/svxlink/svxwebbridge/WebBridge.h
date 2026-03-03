/**
@file    WebBridge.h
@brief   Bridge between an SvxReflector and WebSocket clients
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

#ifndef WEB_BRIDGE_H
#define WEB_BRIDGE_H


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <cstdint>
#include <string>
#include <set>
#include <map>


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

#include "../reflectorclient/ReflectorClient.h"
#include "WsServer.h"


/****************************************************************************
 *
 * Forward declarations
 *
 ****************************************************************************/

class WsClient;


/****************************************************************************
 *
 * Class definitions
 *
 ****************************************************************************/

/**
@brief  Connects an SvxReflector to WebSocket clients

Extends ReflectorClient so it can connect to any SvxReflector as an
authenticated node.  Runs a WsServer in the same event loop to accept
browser clients over WebSocket.

Browser clients send {"type":"subscribe","tg":9} to choose a TG.
Audio received from the reflector is forwarded as binary WebSocket frames.
All talker-start/stop and node-join/leave events are forwarded as JSON.

Additional configuration keys (beyond those of ReflectorClient):
  WS_PORT             – WebSocket listen port (default 8080)
  MONITOR_TGS         – comma/space-separated list of TG numbers to monitor
                        at startup (the union of all web clients' subscriptions
                        is added dynamically on top of this set)
*/
class WebBridge : public ReflectorClient
{
  public:
    WebBridge(void);
    ~WebBridge(void) override;

    /**
     * @brief   Initialize the bridge
     * @param   cfg     Configuration object
     * @param   section Config section name
     * @return  true on success
     */
    bool initialize(Async::Config& cfg, const std::string& section);

  protected:
    void onLoggedIn(void)  override;
    void onDisconnected(void) override;

    void onAudioReceived(uint32_t tg, const std::string& codec,
                         const void* data, int len) override;
    void onAudioFlushed(uint32_t tg) override;

    void onTalkerStart(uint32_t tg, const std::string& callsign) override;
    void onTalkerStop(uint32_t tg, const std::string& callsign) override;

    void onNodeJoined(const std::string& callsign) override;
    void onNodeLeft(const std::string& callsign) override;

    void onQsyRequest(uint32_t tg) override;

    Json::Value buildNodeInfo(void) const override;

  private:
    WsServer                        m_ws_server;
    std::set<uint32_t>              m_static_monitor_tgs;
    std::map<uint32_t, std::string> m_active_talkers; // tg → callsign
    std::map<uint32_t, int>         m_tg_refcounts;   // tg → number of web subscribers
    std::string                     m_section;

    WebBridge(const WebBridge&) = delete;
    WebBridge& operator=(const WebBridge&) = delete;

    void onWsClientSubscribed(WsClient* client, uint32_t tg);
    void onWsClientUnsubscribed(WsClient* client);

    void updateMonitorTgs(void);
    void broadcastTgList(void);
    void sendTgListTo(WsClient* client);
    void sendCodecInfoTo(WsClient* client, uint32_t tg);

}; /* class WebBridge */


#endif /* WEB_BRIDGE_H */


/*
 * This file has not been truncated
 */
