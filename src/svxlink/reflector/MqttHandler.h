/**
@file	 MqttHandler.h
@brief   MQTT handler class
@author  Rui Barreiros / CR7BPM
@date	 2025-08-05

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

#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <mqtt/async_client.h>
#include <mqtt/ssl_options.h>

/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

 /****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

 /****************************************************************************
 *
 * Forward declarations
 *
 ****************************************************************************/
class Reflector;

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
@brief	MQTT handler class
@author Rui Barreiros / CR7BPM
@date   2025-08-05

This is the MQTT handler class for the reflector. It handles all MQTT traffic and
the dispatching of incoming messages to the correct Reflector object.


Convetions:

    - Reflector lwt lives in MQTT_PREFIX/reflector/lwt
    
    - Reflector each node events live in MQTT_PREFIX/reflector/nodes/<callsign>/
      All current connected nodes are published in the MQTT_PREFIX/reflector/nodes/connected as a json array of callsigns

    - Reflector system events live in MQTT_PREFIX/reflector/system/
      .oO(I wonder, should we publish messages ? stdout/stderr ?)

    - Reflector commands are subscribed to MQTT_PREFIX/reflector/commands
        The commands can be sent in JSON or in plain text.
        JSON are sent to MQTT_PREFIX/reflector/commands in the format:
          {"command": "<full command text like PTY"} 
          For example:
          {"command": "CFG GLOBAL REJECT_CALLSIGN N0CALL"}

          you can send help to get the list of commands
          {"command": "HELP"}

        TEXT are sent to MQTT_PREFIX/reflector/commands/command in the same format as PTY
        
          "HELP"
          "CFG GLOBAL REJECT_CALLSIGN N0CALL"
        
    - Reflector answers to commands are sent to MQTT_PREFIX/reflector/commands/reply
        The answer is sent in JSON format:
          {"data": "<full reply text like PTY"} 
          For example:
          {"data": "OK:Status published to MQTT"}

        If the command is not found, the answer is sent in JSON format:
          {"date": "ERR:Unknown MQTT command 'none'. Use HELP for available commands"}


*/
class MqttHandler : public mqtt::callback
{
public:
    /**
     * @brief Constructor
     * @param reflector Pointer to the reflector object
     */
    MqttHandler(Reflector* reflector);

    /**
     * @brief Destructor
     */
    ~MqttHandler();
    
    /**
     * @brief Initialize the MQTT handler
     * @param broker_host MQTT broker host
     * @param broker_port MQTT broker port
     * @param username MQTT username
     * @param password MQTT password
     * @param reflector_id Reflector ID
     * @param topic_prefix MQTT topic prefix
     * @param ssl_enabled Enable SSL/TLS connection
     * @param ca_cert_file CA certificate file path (optional)
     * @param client_cert_file Client certificate file path (optional)
     * @param client_key_file Client key file path (optional)
     * @param ssl_verify_hostname Verify SSL hostname
     * @return True if initialization was successful, false otherwise
     */
    bool init(const std::string& broker_host, int broker_port,
              const std::string& username, const std::string& password,
              const std::string& reflector_id, const std::string& topic_prefix,
              bool ssl_enabled = false, const std::string& ca_cert_file = "",
              const std::string& client_cert_file = "", const std::string& client_key_file = "",
              bool ssl_verify_hostname = true);

    /**
     * @brief Reconnect to the MQTT broker
     * @return True if reconnection was successful, false otherwise
     */
    bool reconnect();

    /**
     * @brief Publish node event to the MQTT broker
     * @param event_type Event type
     * @param data Event data
     */
    void publishNodeEvent(const std::string& event_type,
                           const std::string& callsign,
                           Json::Value& data);

    /**
     * @brief Publish nodes to the MQTT broker
     * @param data data
     */
    void publishNodes(Json::Value& data);

                           /**
     * @brief Publish system event to the MQTT broker
     * @param event_type Event type
     * @param data Event data
     */
    void publishSystemEvent(const std::string& event_type,
                           Json::Value& data);

    /**
     * @brief Publish command reply to the MQTT broker
     * @param data Reply
     */
    void publishCommandReply(Json::Value& data);

    /**
     * @brief Publish heartbeat to the MQTT broker
     * @param uptime_seconds Uptime in seconds
     */
    void publishHeartbeat(int uptime_seconds);

    /** 
     * @brief Check if the MQTT client is connected (thread-safe version)
     * @return True if the MQTT client is connected, false otherwise
     */
    bool isConnectedSafe() const;

    /** 
     * @brief Check if the MQTT client is connected (DEPRECATED)
     * 
     * BEWARE!!!!!! This function is not thread safe and cannot be called
     * from within a callback!!!! IT WILL HANG!!!!
     * 
     * @deprecated Use isConnectedSafe() instead for thread safety
     * @return True if the MQTT client is connected, false otherwise
     */
    bool isConnected() const;

    /**
     * @brief Set the command callback
     * @param callback Command callback
     */
    void setCommandCallback(std::function<void(const std::string&)> callback) {
        m_command_callback = callback;
    }

protected:

    /**
     * @brief Message arrived callback
     * @param msg Message pointer
     */
    void message_arrived(mqtt::const_message_ptr msg) override;
    
    /**
     * @brief Connected callback
     * @param cause Connection cause
     */
    void connected(const std::string& cause) override;

    /**
     * @brief Connection lost callback
     * @param cause Connection lost cause
     */
    void connection_lost(const std::string& cause) override;

    /**
     * @brief Delivery complete callback
     * @param token Delivery token
     */
    void delivery_complete(mqtt::delivery_token_ptr token) override;

private:
    /**
     * @brief Connect to the MQTT broker
     * @return True if connection was successful, false otherwise
     */
    bool connect();

    /**
     * @brief Subscribe to commands
     */
    void subscribeToCommands();

    /**
     * @brief Publish a topic to the MQTT broker
     * @param topic Topic
     * @param data Data
     */
    void publishTopic(const std::string& topic, const std::string& data, int qos = 0);


    /**
     * @brief Build a topic
     */
    std::string buildTopic(const std::string& topic_type) const;

    /**
     * @brief Get the current timestamp
     * @return Current timestamp
     */
    static std::string getCurrentTimestamp();

    /**
     * @brief Reflector pointer
     */
    Reflector* m_reflector;

    /**
     * @brief MQTT client
     */
    std::unique_ptr<mqtt::async_client> m_client;

    /**
     * @brief MQTT connection options
     */
    mqtt::connect_options m_conn_opts;

    /**
     * @brief MQTT initialized flag
     */
    bool m_initialized;

    /**
     * @brief Reflector ID
     */
    std::string m_reflector_id;

    /**
     * @brief MQTT topic prefix
     */
    std::string m_topic_prefix;

    /**
     * @brief Command callback
     */
    std::function<void(const std::string&)> m_command_callback;

    /**
     * @brief Thread safety mutex
     */
    mutable std::mutex m_mutex;

    /**
     * @brief Thread-safe connection state tracking
     */
    std::atomic<bool> m_connected_state;
};

#endif // MQTT_HANDLER_H 


/*
 * This file has not been truncated
 */
