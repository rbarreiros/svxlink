/**
@file    WsServer.h
@brief   WebSocket server for svxwebbridge
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

#ifndef WS_SERVER_H
#define WS_SERVER_H


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncTcpServer.h>
#include <AsyncTcpConnection.h>
#include <sigc++/sigc++.h>
#include <json/json.h>


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
@brief  Manages a single WebSocket client connection

Handles the HTTP upgrade handshake, WebSocket frame parsing and framing.
Text frames are delivered as JSON.  Binary frames are assembled and
delivered to the application layer.

The connection is owned by WsServer and deleted when it closes.
*/
class WsClient : public sigc::trackable
{
  public:
    /**
     * @brief   Emitted when the client sends a complete JSON text message
     */
    sigc::signal<void, WsClient*, Json::Value> messageReceived;

    /**
     * @brief   Emitted when the connection is fully closed
     */
    sigc::signal<void, WsClient*> disconnected;

    /**
     * @brief   Send a JSON object as a WebSocket text frame
     */
    void sendJson(const Json::Value& val);

    /**
     * @brief   Send raw bytes as a WebSocket binary frame
     * @param   data  Pointer to data bytes
     * @param   len   Number of bytes
     */
    void sendBinary(const void* data, size_t len);

    /**
     * @brief   Send an audio frame tagged with a TG ID
     *
     * Binary frame layout:
     *   [0x01][TG id, 4 bytes BE][codec payload]
     */
    void sendAudio(uint32_t tg, const void* data, int len);

    /**
     * @brief   Send an end-of-transmission marker for a TG
     *
     * Binary frame layout:
     *   [0x02][TG id, 4 bytes BE]
     */
    void sendFlush(uint32_t tg);

    uint32_t subscribedTg(void) const { return m_subscribed_tg; }
    void setSubscribedTg(uint32_t tg)  { m_subscribed_tg = tg; }

    /**
     * @brief   Gracefully close the WebSocket connection
     */
    void close(void);

  private:
    friend class WsServer;

    explicit WsClient(Async::TcpConnection* con);
    ~WsClient(void) = default;

    typedef enum
    {
      STATE_HTTP_HANDSHAKE,
      STATE_WS_CONNECTED,
      STATE_CLOSING
    } State;

    Async::TcpConnection*   m_con;
    State                   m_state        = STATE_HTTP_HANDSHAKE;
    std::string             m_http_buf;
    std::vector<uint8_t>    m_ws_buf;
    uint32_t                m_subscribed_tg = 0;

    Json::CharReaderBuilder m_json_reader_builder;

    WsClient(const WsClient&) = delete;
    WsClient& operator=(const WsClient&) = delete;

    int  onDataReceived(Async::TcpConnection*, void* buf, int count);
    void onDisconnected(Async::TcpConnection*,
                        Async::TcpConnection::DisconnectReason);

    bool processHttpHandshake(void);
    bool parseWsFrames(void);

    std::string computeAccept(const std::string& key) const;
    void sendWsFrame(uint8_t opcode, const void* data, size_t len);
    void sendWsPong(const void* data, size_t len);

}; /* class WsClient */


/**
@brief  WebSocket server built on Async::TcpServer

Listens for incoming TCP connections, performs the HTTP/WebSocket
upgrade, then routes audio and JSON events to subscribed clients.

Audio frame binary wire format (server → client):
  Byte 0    : 0x01 = audio payload, 0x02 = flush marker
  Bytes 1-4 : Talk-group ID, big-endian uint32
  Bytes 5+  : Raw codec bytes (audio only; absent for flush)

JSON text frames (server → client) carry control/metadata events
(talker_start/stop, node_joined/left, tg_list, etc.)
*/
class WsServer : public sigc::trackable
{
  public:
    WsServer(void);
    ~WsServer(void);

    /**
     * @brief   Start listening on the given port
     * @param   port    TCP port to listen on
     * @return  true on success
     */
    bool initialize(uint16_t port);

    /**
     * @brief   Send an audio frame to all clients subscribed to @a tg
     */
    void sendAudio(uint32_t tg, const std::string& codec,
                   const void* data, int len);

    /**
     * @brief   Send an end-of-transmission marker to all subscribers of @a tg
     */
    void sendFlush(uint32_t tg);

    /**
     * @brief   Broadcast a JSON message to all connected clients
     */
    void broadcastJson(const Json::Value& val);

    /**
     * @brief   Send a JSON message only to clients subscribed to @a tg
     */
    void sendJsonToTg(uint32_t tg, const Json::Value& val);

    /**
     * @brief   Emitted when a client subscribes to a TG
     */
    sigc::signal<void, WsClient*, uint32_t> clientSubscribed;

    /**
     * @brief   Emitted when a client unsubscribes / disconnects
     */
    sigc::signal<void, WsClient*> clientUnsubscribed;

  private:
    Async::TcpServer<Async::TcpConnection>* m_server = nullptr;
    std::set<WsClient*>                      m_clients;

    WsServer(const WsServer&) = delete;
    WsServer& operator=(const WsServer&) = delete;

    void onClientConnected(Async::TcpConnection* con);
    void onClientMessage(WsClient* client, Json::Value msg);
    void onClientDisconnected(WsClient* client);

}; /* class WsServer */


#endif /* WS_SERVER_H */


/*
 * This file has not been truncated
 */
