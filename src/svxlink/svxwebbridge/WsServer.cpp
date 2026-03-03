/**
@file    WsServer.cpp
@brief   WebSocket server for svxwebbridge
@author  Ricardo Barreiros / CT7ALW
*/


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <openssl/sha.h>
#include <openssl/evp.h>

#include <sstream>
#include <iostream>
#include <algorithm>
#include <cassert>
#include <cstring>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncTcpServer.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

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
 * File-local helpers
 *
 ****************************************************************************/

namespace {

  std::string base64Encode(const unsigned char* data, size_t len)
  {
    std::string out;
    out.resize(((len + 2) / 3) * 4);
    int written = EVP_EncodeBlock(
        reinterpret_cast<unsigned char*>(&out[0]), data, static_cast<int>(len));
    out.resize(written);
    return out;
  }

  void writeU32BE(uint8_t* dst, uint32_t v)
  {
    dst[0] = (v >> 24) & 0xFF;
    dst[1] = (v >> 16) & 0xFF;
    dst[2] = (v >>  8) & 0xFF;
    dst[3] = (v      ) & 0xFF;
  }

} // namespace


/****************************************************************************
 *
 * WsClient public methods
 *
 ****************************************************************************/

void WsClient::sendJson(const Json::Value& val)
{
  if (m_state != STATE_WS_CONNECTED)
  {
    return;
  }
  Json::StreamWriterBuilder builder;
  builder["commentStyle"] = "None";
  builder["indentation"] = "";
  std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
  std::ostringstream oss;
  writer->write(val, &oss);
  const std::string& str = oss.str();
  sendWsFrame(0x81, str.data(), str.size()); // 0x81 = FIN + text opcode
} /* WsClient::sendJson */


void WsClient::sendBinary(const void* data, size_t len)
{
  if (m_state != STATE_WS_CONNECTED)
  {
    return;
  }
  sendWsFrame(0x82, data, len); // 0x82 = FIN + binary opcode
} /* WsClient::sendBinary */


void WsClient::sendAudio(uint32_t tg, const void* data, int len)
{
  if (m_state != STATE_WS_CONNECTED || len <= 0)
  {
    return;
  }
  std::vector<uint8_t> frame(5 + static_cast<size_t>(len));
  frame[0] = 0x01; // audio
  writeU32BE(&frame[1], tg);
  memcpy(&frame[5], data, static_cast<size_t>(len));
  sendBinary(frame.data(), frame.size());
} /* WsClient::sendAudio */


void WsClient::sendFlush(uint32_t tg)
{
  if (m_state != STATE_WS_CONNECTED)
  {
    return;
  }
  uint8_t frame[5];
  frame[0] = 0x02; // flush
  writeU32BE(&frame[1], tg);
  sendBinary(frame, sizeof(frame));
} /* WsClient::sendFlush */


void WsClient::close(void)
{
  if (m_state == STATE_CLOSING)
  {
    return;
  }
  m_state = STATE_CLOSING;
  if (m_state == STATE_WS_CONNECTED)
  {
      // Send WS close frame (opcode 0x88)
    sendWsFrame(0x88, nullptr, 0);
  }
  m_con->disconnect();
} /* WsClient::close */


/****************************************************************************
 *
 * WsClient private methods
 *
 ****************************************************************************/

WsClient::WsClient(TcpConnection* con) : m_con(con)
{
  m_con->dataReceived.connect(
      sigc::mem_fun(*this, &WsClient::onDataReceived));
  m_con->disconnected.connect(
      sigc::mem_fun(*this, &WsClient::onDisconnected));
} /* WsClient::WsClient */


int WsClient::onDataReceived(TcpConnection*, void* buf, int count)
{
  if (m_state == STATE_HTTP_HANDSHAKE)
  {
    m_http_buf.append(reinterpret_cast<const char*>(buf), count);
    if (m_http_buf.find("\r\n\r\n") != std::string::npos)
    {
      if (!processHttpHandshake())
      {
        m_con->disconnect();
      }
    }
    return count;
  }

  if (m_state == STATE_WS_CONNECTED)
  {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(buf);
    m_ws_buf.insert(m_ws_buf.end(), p, p + count);
    parseWsFrames();
  }
  return count;
} /* WsClient::onDataReceived */


void WsClient::onDisconnected(TcpConnection*,
                               TcpConnection::DisconnectReason)
{
  m_state = STATE_CLOSING;
  disconnected.emit(this);
} /* WsClient::onDisconnected */


bool WsClient::processHttpHandshake(void)
{
    // Extract the Sec-WebSocket-Key header value
  auto keyPos = m_http_buf.find("Sec-WebSocket-Key:");
  if (keyPos == std::string::npos)
  {
      // Case-insensitive fallback
    keyPos = m_http_buf.find("sec-websocket-key:");
  }
  if (keyPos == std::string::npos)
  {
    std::cerr << "WsClient: No Sec-WebSocket-Key in request" << std::endl;
    return false;
  }
  keyPos += 18; // length of "Sec-WebSocket-Key:"
  auto keyEnd = m_http_buf.find("\r\n", keyPos);
  if (keyEnd == std::string::npos)
  {
    return false;
  }
  std::string key = m_http_buf.substr(keyPos, keyEnd - keyPos);
    // Trim whitespace
  const auto trim = [](std::string& s)
  {
    s.erase(s.begin(),
            std::find_if(s.begin(), s.end(),
                         [](unsigned char c){ return !std::isspace(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(),
                         [](unsigned char c){ return !std::isspace(c); }).base(),
            s.end());
  };
  trim(key);

  if (key.empty())
  {
    std::cerr << "WsClient: Empty Sec-WebSocket-Key" << std::endl;
    return false;
  }

  std::string accept = computeAccept(key);

  std::ostringstream response;
  response << "HTTP/1.1 101 Switching Protocols\r\n"
           << "Upgrade: websocket\r\n"
           << "Connection: Upgrade\r\n"
           << "Sec-WebSocket-Accept: " << accept << "\r\n"
           << "\r\n";

  const std::string& resp_str = response.str();
  m_con->write(resp_str.data(), resp_str.size());
  m_state = STATE_WS_CONNECTED;
  m_http_buf.clear();
  return true;
} /* WsClient::processHttpHandshake */


bool WsClient::parseWsFrames(void)
{
  while (!m_ws_buf.empty())
  {
    if (m_ws_buf.size() < 2)
    {
      break; // need more data
    }

    uint8_t byte0 = m_ws_buf[0];
    uint8_t byte1 = m_ws_buf[1];

    bool fin    = (byte0 & 0x80) != 0;
    uint8_t opcode = byte0 & 0x0F;
    bool masked = (byte1 & 0x80) != 0;
    uint64_t payload_len = byte1 & 0x7F;

    size_t header_len = 2;
    if (payload_len == 126)
    {
      if (m_ws_buf.size() < 4) { break; }
      payload_len = (static_cast<uint64_t>(m_ws_buf[2]) << 8) | m_ws_buf[3];
      header_len = 4;
    }
    else if (payload_len == 127)
    {
      if (m_ws_buf.size() < 10) { break; }
      payload_len = 0;
      for (int i = 0; i < 8; ++i)
      {
        payload_len = (payload_len << 8) | m_ws_buf[2 + i];
      }
      header_len = 10;
    }

    if (masked) { header_len += 4; }

    if (m_ws_buf.size() < header_len + payload_len)
    {
      break; // frame not yet complete
    }

    uint8_t masking_key[4] = {0, 0, 0, 0};
    if (masked)
    {
      memcpy(masking_key, &m_ws_buf[header_len - 4], 4);
    }

    std::vector<uint8_t> payload(payload_len);
    for (uint64_t i = 0; i < payload_len; ++i)
    {
      payload[i] = m_ws_buf[header_len + i] ^ (masked ? masking_key[i % 4] : 0);
    }

      // Consume the frame from the buffer
    m_ws_buf.erase(m_ws_buf.begin(),
                   m_ws_buf.begin() + static_cast<ptrdiff_t>(header_len + payload_len));

    switch (opcode)
    {
      case 0x01: // text frame
      {
        const std::string text(payload.begin(), payload.end());
        std::string errs;
        Json::Value val;
        std::unique_ptr<Json::CharReader> reader(
            m_json_reader_builder.newCharReader());
        if (reader->parse(text.data(), text.data() + text.size(), &val, &errs))
        {
          messageReceived.emit(this, val);
        }
        else
        {
          std::cerr << "WsClient: JSON parse error: " << errs << std::endl;
        }
        break;
      }
      case 0x08: // close
        m_state = STATE_CLOSING;
        sendWsFrame(0x88, nullptr, 0);
        m_con->disconnect();
        return false;
      case 0x09: // ping
        sendWsPong(payload.data(), payload.size());
        break;
      case 0x0A: // pong
        break;
      default:
        break;
    }

    (void)fin; // fragmented frames not needed for our use case
  }
  return true;
} /* WsClient::parseWsFrames */


std::string WsClient::computeAccept(const std::string& key) const
{
  static const char* WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  std::string cat = key + WS_GUID;
  unsigned char digest[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const unsigned char*>(cat.data()), cat.size(), digest);
  return base64Encode(digest, SHA_DIGEST_LENGTH);
} /* WsClient::computeAccept */


void WsClient::sendWsFrame(uint8_t first_byte, const void* data, size_t len)
{
  std::vector<uint8_t> frame;
  frame.reserve(10 + len);
  frame.push_back(first_byte);

  if (len < 126)
  {
    frame.push_back(static_cast<uint8_t>(len));
  }
  else if (len < 65536)
  {
    frame.push_back(126);
    frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(len & 0xFF));
  }
  else
  {
    frame.push_back(127);
    for (int i = 7; i >= 0; --i)
    {
      frame.push_back(static_cast<uint8_t>((len >> (8 * i)) & 0xFF));
    }
  }

  if (data != nullptr && len > 0)
  {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
    frame.insert(frame.end(), p, p + len);
  }

  m_con->write(frame.data(), frame.size());
} /* WsClient::sendWsFrame */


void WsClient::sendWsPong(const void* data, size_t len)
{
  sendWsFrame(0x8A, data, len); // 0x8A = FIN + pong opcode
} /* WsClient::sendWsPong */


/****************************************************************************
 *
 * WsServer public methods
 *
 ****************************************************************************/

WsServer::WsServer(void) {}


WsServer::~WsServer(void)
{
  for (auto* client : m_clients)
  {
    delete client;
  }
  delete m_server;
} /* WsServer::~WsServer */


bool WsServer::initialize(uint16_t port)
{
  m_server = new TcpServer<TcpConnection>(std::to_string(port));
  m_server->clientConnected.connect(
      sigc::mem_fun(*this, &WsServer::onClientConnected));
  std::cout << "WsServer: Listening on port " << port << std::endl;
  return true;
} /* WsServer::initialize */


void WsServer::sendAudio(uint32_t tg, const std::string& /*codec*/,
                         const void* data, int len)
{
  for (auto* client : m_clients)
  {
    if (client->subscribedTg() == tg)
    {
      client->sendAudio(tg, data, len);
    }
  }
} /* WsServer::sendAudio */


void WsServer::sendFlush(uint32_t tg)
{
  for (auto* client : m_clients)
  {
    if (client->subscribedTg() == tg)
    {
      client->sendFlush(tg);
    }
  }
} /* WsServer::sendFlush */


void WsServer::broadcastJson(const Json::Value& val)
{
  for (auto* client : m_clients)
  {
    client->sendJson(val);
  }
} /* WsServer::broadcastJson */


void WsServer::sendJsonToTg(uint32_t tg, const Json::Value& val)
{
  for (auto* client : m_clients)
  {
    if (client->subscribedTg() == tg)
    {
      client->sendJson(val);
    }
  }
} /* WsServer::sendJsonToTg */


/****************************************************************************
 *
 * WsServer private methods
 *
 ****************************************************************************/

void WsServer::onClientConnected(TcpConnection* con)
{
  std::cout << "WsServer: New connection from "
            << con->remoteHost() << ":" << con->remotePort() << std::endl;
  auto* client = new WsClient(con);
  m_clients.insert(client);
  client->messageReceived.connect(
      sigc::mem_fun(*this, &WsServer::onClientMessage));
  client->disconnected.connect(
      sigc::mem_fun(*this, &WsServer::onClientDisconnected));
} /* WsServer::onClientConnected */


void WsServer::onClientMessage(WsClient* client, Json::Value msg)
{
  const std::string type = msg.get("type", "").asString();

  if (type == "subscribe")
  {
    uint32_t tg = msg.get("tg", 0).asUInt();
    if (tg > 0)
    {
      uint32_t old_tg = client->subscribedTg();
      if (old_tg != 0 && old_tg != tg)
      {
        clientUnsubscribed.emit(client);
      }
      client->setSubscribedTg(tg);
      clientSubscribed.emit(client, tg);
    }
  }
  else if (type == "unsubscribe")
  {
    if (client->subscribedTg() != 0)
    {
      clientUnsubscribed.emit(client);
      client->setSubscribedTg(0);
    }
  }
  else if (type == "ping")
  {
    Json::Value pong;
    pong["type"] = "pong";
    client->sendJson(pong);
  }
} /* WsServer::onClientMessage */


void WsServer::onClientDisconnected(WsClient* client)
{
  std::cout << "WsServer: Client disconnected" << std::endl;
  if (client->subscribedTg() != 0)
  {
    clientUnsubscribed.emit(client);
  }
  m_clients.erase(client);
  delete client;
} /* WsServer::onClientDisconnected */


/*
 * This file has not been truncated
 */
