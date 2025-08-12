/**
@file	 ReflectorClient.h
@brief   Represents one client connection
@author  Tobias Blomberg / SM0SVX
@date	 2017-02-11

\verbatim
SvxReflector - An audio reflector for connecting SvxLink Servers
Copyright (C) 2003-2025 Tobias Blomberg / SM0SVX

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

#ifndef REFLECTOR_CLIENT_INCLUDED
#define REFLECTOR_CLIENT_INCLUDED


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <string>
#include <json/json.h>
#include <sigc++/sigc++.h>
#include <random>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncFramedTcpConnection.h>
#include <AsyncTimer.h>
#include <AsyncAtTimer.h>
#include <AsyncConfig.h>
#include <AsyncSslCertSigningReq.h>
#include <AsyncSslX509.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "ReflectorMsg.h"
#include "ProtoVer.h"

/****************************************************************************
 *
 * Forward declarations
 *
 ****************************************************************************/
#ifdef HAVE_MQTT
class MqttHandler;
#endif


/****************************************************************************
 *
 * Forward declarations of classes inside of the declared namespace
 *
 ****************************************************************************/

class Reflector;


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
@brief	Represents one client connection
@author Tobias Blomberg / SM0SVX
@date   2017-02-11

This class represents one client connection. When a client connects, an
instance of this class will be created that will persist for the lifetime of
the client connection.
*/
class ReflectorClient : public sigc::trackable
{
  public:
    using ClientId = ReflectorUdpMsg::ClientId;
    using ClientSrc = std::pair<Async::IpAddress, uint16_t>;

    typedef enum
    {
      STATE_EXPECT_DISCONNECT,
      STATE_DISCONNECTED,
      STATE_EXPECT_PROTO_VER,
      STATE_EXPECT_START_ENCRYPTION,
      STATE_EXPECT_SSL_CON_READY,
      STATE_EXPECT_CSR,
      STATE_EXPECT_AUTH_RESPONSE,
      STATE_CONNECTED
    } ConState;

    class Filter
    {
      public:
        virtual ~Filter(void) {}
        virtual bool operator ()(ReflectorClient *client) const = 0;
    };

    class NoFilter : public Filter
    {
      public:
        virtual bool operator ()(ReflectorClient *) const { return true; }
    };

    class ExceptFilter : public Filter
    {
      public:
        ExceptFilter(const ReflectorClient* except) : m_except(except) {}
        virtual bool operator ()(ReflectorClient *client) const
        {
          return client != m_except;
        }
      private:
        const ReflectorClient* m_except;
    };

    class ProtoVerRangeFilter : public Filter
    {
      public:
        ProtoVerRangeFilter(const ProtoVerRange& pvr) : m_pv_range(pvr) {}
        ProtoVerRangeFilter(const ProtoVer& min, const ProtoVer& max)
          : m_pv_range(min, max) {}
        virtual bool operator ()(ReflectorClient *client) const
        {
          return (!m_pv_range.isValid() ||
                 m_pv_range.isWithinRange(client->protoVer()));
        }
      private:
        ProtoVerRange m_pv_range;
    };

    class ProtoVerLargerOrEqualFilter : public Filter
    {
      public:
        ProtoVerLargerOrEqualFilter(const ProtoVer& min) : m_pv(min) {}
        virtual bool operator ()(ReflectorClient *client) const
        {
          return (client->protoVer() >= m_pv);
        }
      private:
        ProtoVer m_pv;
    };

    class TgFilter : public Filter
    {
      public:
        TgFilter(uint32_t tg) : m_tg(tg) {}
        virtual bool operator ()(ReflectorClient *client) const;
      private:
        uint32_t m_tg;
    };

    class TgMonitorFilter : public Filter
    {
      public:
        TgMonitorFilter(uint32_t tg) : m_tg(tg) {}
        virtual bool operator ()(ReflectorClient *client) const
        {
          return client->m_monitored_tgs.count(m_tg) > 0;
        }
      private:
        uint32_t m_tg;
    };

    template <class F1, class F2>
    class AndFilter : public Filter
    {
      public:
        AndFilter(const F1& f1, const F2& f2) : m_f1(f1), m_f2(f2) {}
        virtual bool operator ()(ReflectorClient *client) const
        {
          return m_f1(client) && m_f2(client);
        }
      private:
        F1 m_f1;
        F2 m_f2;
    };

    template <class F1, class F2>
    static AndFilter<F1, F2> mkAndFilter(const F1& f1, const F2& f2)
    {
      return AndFilter<F1, F2>(f1, f2);
    }

    template <class F1, class F2>
    class OrFilter : public Filter
    {
      public:
        OrFilter(const F1& f1, const F2& f2) : m_f1(f1), m_f2(f2) {}
        virtual bool operator ()(ReflectorClient *client) const
        {
          return m_f1(client) || m_f2(client);
        }
      private:
        F1 m_f1;
        F2 m_f2;
    };

    template <class F1, class F2>
    static OrFilter<F1, F2> mkOrFilter(const F1& f1, const F2& f2)
    {
      return OrFilter<F1, F2>(f1, f2);
    }

    /**
     * @brief   Get the client object associated with the given id
     * @param   id The id of the client object to find
     * @return  Return the client object associated with the given id
     */
    static ReflectorClient* lookup(const ClientId& id);

    /**
     * @brief   Get the client object associated with the given source addr
     * @param   src The source address of the client object to find
     * @return  Return the client object associated with the given source addr
     */
    static ReflectorClient* lookup(const ClientSrc& src);

    /**
     * @brief   Get the client object associated with the given callsign
     * @param   cs The callsign of the client object to find
     * @return  Return the client object associated with the given callsign
     */
    static ReflectorClient* lookup(const std::string& cs);

    /**
     * @brief   Remove all client objects
     */
    static void cleanup(void);

    /**
     * @brief 	Constructor
     * @param   ref The associated Reflector object
     * @param   con The associated FramedTcpConnection object
     * @param   cfg The associated configuration file object
     * @param   mqtt_handler Pointer to the MQTT handler (optional, can be nullptr)
     */
    ReflectorClient(Reflector *ref, Async::FramedTcpConnection *con,
                    Async::Config* cfg
#ifdef HAVE_MQTT
                    , MqttHandler* mqtt_handler = nullptr
#endif
                    );

    /**
     * @brief 	Destructor
     */
    ~ReflectorClient(void);

    /**
     * @brief 	Return the client ID
     * @return	Returns the client ID
     *
     * The client ID is a unique number assigned to each connected client.
     * It is for example used to associate incoming audio with the correct
     * client.
     */
    ClientId clientId(void) const { return m_client_id; }

    /**
     * @brief   Get the local IP address associated with this connection
     * @return  Returns an IP address
     */
    Async::IpAddress localHost(void) const
    {
      return m_con->localHost();
    }

    /**
     * @brief   Get the local TCP port associated with this connection
     * @return  Returns a port number
     */
    uint16_t localPort(void) const
    {
      return m_con->localPort();
    }

    /**
     * @brief   Return the remote IP address
     * @return  Returns the IP address of the client
     */
    const Async::IpAddress& remoteHost(void) const
    {
      return m_con->remoteHost();
    }

    uint16_t remotePort(void) const
    {
      return m_con->remotePort();
    }

    /**
     * @brief   Return the remote UDP IP
     * @return  Returns the source IP used by the client for UDP
     */
    const Async::IpAddress& remoteUdpHost(void) const
    {
      const auto& addr = m_client_src.first;
      if (addr.isEmpty())
      {
        return remoteHost();
      }
      return addr;
    }

    /**
     * @brief   Return the remote port number
     * @return  Returns the source port used by the client for UDP
     */
    uint16_t remoteUdpPort(void) const { return m_remote_udp_port; }

    /**
     * @brief   Set the remote UDP source (IP, port)
     * @param   src A ClientSrc
     *
     * The Reflector use this function to set the (IP, port number) used by the
     * client so that UDP packets can be sent to the client, incoming UDP
     * packets can be associated with the correct client object and to check
     * that incoming packets originate from the correct source.
     */
    void setRemoteUdpSource(const ClientSrc& src);

    /**
     * @brief   Get the callsign for this connection
     * @return  Returns the callsign associated with this coinnection
     */
    const std::string& callsign(void) const { return m_callsign; }

    /**
     * @brief   Return the next UDP packet transmit sequence number
     * @return  Returns the UDP packet sequence number that should be used next
     *
     * This function will return the UDP packet sequence number that should be
     * used next. The squence number is incremented when this function is called
     * so it can only be called one time per packet. The sequence number is
     * used by the receiver to find out if a packet is out of order or if a
     * packet has been lost in transit.
     */
    //uint16_t nextUdpTxSeq(void) { return m_next_udp_tx_seq++; }

    /**
     * @brief   Set the UDP RX sequence number
     */
    void setUdpRxSeq(UdpCipher::IVCntr seq) { m_next_udp_rx_seq = seq; }

    /**
     * @brief   Get the next expected UDP packet sequence number
     * @return  Returns the next expected UDP packet sequence number
     *
     * This function will return the next expected UDP sequence number, which
     * is simply the previously received sequence number plus one.
     */
    UdpCipher::IVCntr nextUdpRxSeq(void) { return m_next_udp_rx_seq; }

    /**
     * @brief   Send a TCP message to the remote end
     * @param   The mesage to send
     * @return  On success 0 is returned or else -1
     */
    int sendMsg(const ReflectorMsg& msg);

    /**
     * @brief   Handle a received UDP message
     * @param   The received UDP message
     *
     * This function is called by the Reflector when a UDP packet is received.
     * The purpose is to handle packet related timers and sequence numbers.
     */
    void udpMsgReceived(const ReflectorUdpMsg &header);

    /**
     * @brief   Send a UDP message to the client
     * @param   The message to send
     */
    void sendUdpMsg(const ReflectorUdpMsg &msg);

    /**
     * @brief   Block client audio for the specified time
     * @param   The number of seconds to block
     *
     * This function is used to block the client from sending audio for the
     * specified time. This is used by the Reflector if a client has been
     * talking for too long.
     */
    void setBlock(unsigned blocktime);

    /**
     * @brief   Check if a client is blocked
     * @return  Returns \em true if the client is blocked or else \em false
     */
    bool isBlocked(void) const { return (m_remaining_blocktime > 0); }

    /**
     * @brief   Get the state of the connection
     * @return  Returns the state of the connection
     */
    ConState conState(void) const { return m_con_state; }

    /**
     * @brief   Get the protocol version of the client
     * @return  Returns the protocol version of the client
     */
    const ProtoVer& protoVer(void) const { return m_client_proto_ver; }

    /**
     * @brief   Get the current talk group
     * @return  Returns the currently selected talk group
     */
    uint32_t currentTG(void) const { return m_current_tg; }

    /**
     * @brief   Get the monitored talk groups
     * @return  Returns the monitored talk groups
     */
    const std::set<uint32_t>& monitoredTGs(void) const
    {
      return m_monitored_tgs;
    }

    std::vector<char> rxIdList(void) const
    {
      std::vector<char> ids;
      ids.reserve(m_json_rx_map.size());
      for (const auto& rx : m_json_rx_map)
      {
        ids.push_back(rx.first);
      }
      return ids;
    }
    void setRxSiglev(char id, uint8_t siglev)
    {
      setRxParam(id, "siglev", siglev);
    }
    void setRxEnabled(char id, bool enab) { setRxParam(id, "enabled", enab); }
    void setRxSqlOpen(char id, bool open) { setRxParam(id, "sql_open", open); }
    void setRxActive(char id, bool active) { setRxParam(id, "active", active); }

    void setTxTransmit(char id, bool transmit)
    {
      setTxParam(id, "transmit", transmit);
    }

    void updateIsTalker(void);

    /**
     * @brief   Get the duration of the current talking session
     *          Added by Rui Barreiros | CR7BPM for talk duration in MQTT
     * @return  Returns the duration in seconds, or 0 if not currently talking
     */
    double getTalkingDuration(void) const;

    uint32_t udpCipherIVCntrNext() { return m_udp_cipher_iv_cntr++; }
    std::vector<uint8_t> udpCipherIV(void) const;

    void setUdpCipherIVRand(const std::vector<uint8_t>& iv_rand)
    {
      m_udp_cipher_iv_rand = iv_rand;
    }
    std::vector<uint8_t> udpCipherIVRand(void) const
    {
      return m_udp_cipher_iv_rand;
    }

    void setUdpCipherKey(const std::vector<uint8_t>& key)
    {
      m_udp_cipher_key = key;
    }
    std::vector<uint8_t> udpCipherKey(void) const { return m_udp_cipher_key; }

    void certificateUpdated(Async::SslX509& cert);

  private:
    using ClientIdRandomDist  = std::uniform_int_distribution<ClientId>;
    using ClientMap           = std::map<ClientId, ReflectorClient*>;
    using ClientSrcMap        = std::map<ClientSrc, ReflectorClient*>;
    using ClientCallsignMap   = std::map<std::string, ReflectorClient*>;
    using JsonRxMap           = std::map<char, Json::Value&>;
    using JsonTxMap           = std::map<char, Json::Value&>;

    static const uint16_t MIN_MAJOR_VER = 0;
    static const uint16_t MIN_MINOR_VER = 6;

    static const unsigned HEARTBEAT_TX_CNT_RESET      = 10;
    static const unsigned HEARTBEAT_RX_CNT_RESET      = 15;
    static const unsigned UDP_HEARTBEAT_TX_CNT_RESET  = 15;
    static const unsigned UDP_HEARTBEAT_RX_CNT_RESET  = 120;

    static const ClientId CLIENT_ID_MAX = std::numeric_limits<ClientId>::max();
    static const ClientId CLIENT_ID_MIN = 1;

    static ClientMap            client_map;
    static ClientSrcMap         client_src_map;
    static ClientCallsignMap    client_callsign_map;
    static std::mt19937         id_gen;
    static ClientIdRandomDist   id_dist;

    Async::FramedTcpConnection* m_con;
    unsigned char               m_auth_challenge[MsgAuthChallenge::LENGTH];
    ConState                    m_con_state;
    Async::Timer                m_disc_timer;
    std::string                 m_callsign;
    ClientId                    m_client_id;
    ClientSrc                   m_client_src;
    uint16_t                    m_remote_udp_port;
    Async::Config*              m_cfg;
    UdpCipher::IVCntr           m_next_udp_rx_seq;
    Async::Timer                m_heartbeat_timer;
    unsigned                    m_heartbeat_tx_cnt;
    unsigned                    m_heartbeat_rx_cnt;
    unsigned                    m_udp_heartbeat_tx_cnt;
    unsigned                    m_udp_heartbeat_rx_cnt;
    Reflector*                  m_reflector;
    unsigned                    m_blocktime;
    unsigned                    m_remaining_blocktime;
    ProtoVer                    m_client_proto_ver;
    std::vector<std::string>    m_supported_codecs;
    uint32_t                    m_current_tg;
    std::set<uint32_t>          m_monitored_tgs;
    JsonRxMap                   m_json_rx_map;
    JsonTxMap                   m_json_tx_map;
    std::vector<uint8_t>        m_udp_cipher_iv_rand;
    std::vector<uint8_t>        m_udp_cipher_key;
    UdpCipher::IVCntr           m_udp_cipher_iv_cntr;
    Async::AtTimer              m_renew_cert_timer;
    Json::Value*                m_status                {nullptr};

    // Added by Rui Barreiros | CR7BPM for talk duration in MQTT
    struct timeval              m_talking_start_time;

#ifdef HAVE_MQTT
    MqttHandler* m_mqtt_handler;
#endif

    static ClientId newClientId(ReflectorClient* client);

    ReflectorClient(const ReflectorClient&);
    ReflectorClient& operator=(const ReflectorClient&);
    void onSslConnectionReady(Async::TcpConnection *con);
    void onFrameReceived(Async::FramedTcpConnection *con,
                         std::vector<uint8_t>& data);
    void handleMsgProtoVer(std::istream& is);
    void handleMsgCABundleRequest(std::istream& is);
    void handleMsgStartEncryptionRequest(std::istream& is);
    void handleMsgAuthResponse(std::istream& is);
    void handleMsgClientCsr(std::istream& is);
    void handleSelectTG(std::istream& is);
    void handleTgMonitor(std::istream& is);
    void handleNodeInfo(std::istream& is);
    void handleMsgSignalStrengthValues(std::istream& is);
    void handleMsgTxStatus(std::istream& is);
    void handleRequestQsy(std::istream& is);
    void handleStateEvent(std::istream& is);
    void handleMsgError(std::istream& is);
    void sendError(const std::string& msg);
    void onDiscTimeout(Async::Timer *t);
    void disconnect(void);
    void handleHeartbeat(Async::Timer *t);
    std::string lookupUserKey(const std::string& callsign);
    void connectionAuthenticated(const std::string& callsign);
    bool sendClientCert(const Async::SslX509& cert);
    void sendAuthChallenge(void);
    void renewClientCertificate(void);
    void setMonitoredTGs(const std::set<uint32_t>& tgs);
    void setTg(uint32_t tg);

    template <typename T>
    void setRxParam(char id, const std::string& name, const T& value)
    {
      auto it = m_json_rx_map.find(id);
      if (it != m_json_rx_map.end())
      {
        (it->second)[name] = value;
      }
    }

    template <typename T>
    void setTxParam(char id, const std::string& name, const T& value)
    {
      auto it = m_json_tx_map.find(id);
      if (it != m_json_tx_map.end())
      {
        (it->second)[name] = value;
      }
    }

};  /* class ReflectorClient */


#endif /* REFLECTOR_CLIENT_INCLUDED */


/*
 * This file has not been truncated
 */
