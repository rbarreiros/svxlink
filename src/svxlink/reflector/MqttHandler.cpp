/**
@file	 MqttHandler.cpp
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

/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <iostream>
#include <sstream>
#include <json/json.h>

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

#include "MqttHandler.h"
#include "Reflector.h"

/****************************************************************************
 *
 * Namespaces to use
 *
 ****************************************************************************/



 /****************************************************************************
 *
 * Defines & typedefs
 *
 ****************************************************************************/



 /****************************************************************************
 *
 * Local class definitions
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Local functions
 *
 ****************************************************************************/



 /****************************************************************************
 *
 * Exported Global Variables
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Local Global Variables
 *
 ****************************************************************************/



 /****************************************************************************
 *
 * Public static functions
 *
 ****************************************************************************/


 /****************************************************************************
 *
 * Public member functions
 *
 ****************************************************************************/

MqttHandler::MqttHandler(Reflector *reflector)
    : m_reflector(reflector), m_initialized(false), m_connected_state(false)
{
}

MqttHandler::~MqttHandler()
{
}

bool MqttHandler::init(const std::string &broker_host, int broker_port,
                       const std::string &username, const std::string &password,
                       const std::string &reflector_id, const std::string &topic_prefix,
                       bool ssl_enabled, const std::string &ca_cert_file,
                       const std::string &client_cert_file, const std::string &client_key_file,
                       bool ssl_verify_hostname)
{
    try
    {
        m_reflector_id = reflector_id;
        m_topic_prefix = topic_prefix;

        // Create client
        std::string protocol = ssl_enabled ? "ssl://" : "tcp://";
        std::string server_uri = protocol + broker_host + ":" + std::to_string(broker_port);
        std::cout << "Creating MQTT client for server: " << server_uri 
                  << (ssl_enabled ? " (SSL enabled)" : " (SSL disabled)") << std::endl;

        m_client = std::unique_ptr<mqtt::async_client>(new mqtt::async_client(server_uri, reflector_id, nullptr));

        // Set connection options
        m_conn_opts.set_keep_alive_interval(60);
        m_conn_opts.set_clean_session(true);
        m_conn_opts.set_user_name(username);
        m_conn_opts.set_password(password);

        // Configure SSL options if enabled
        if (ssl_enabled)
        {
            mqtt::ssl_options ssl_opts;
            
            // Set CA certificate file if provided
            if (!ca_cert_file.empty())
            {
                std::cout << "MQTT SSL: Using CA certificate file: " << ca_cert_file << std::endl;
                ssl_opts.set_trust_store(ca_cert_file);
            }
            
            // Set client certificate and key files if provided
            if (!client_cert_file.empty() && !client_key_file.empty())
            {
                std::cout << "MQTT SSL: Using client certificate: " << client_cert_file << std::endl;
                std::cout << "MQTT SSL: Using client key: " << client_key_file << std::endl;
                ssl_opts.set_key_store(client_cert_file);
                ssl_opts.set_private_key(client_key_file);
            }
            
            // Set hostname verification
            ssl_opts.set_verify(ssl_verify_hostname);
            if (!ssl_verify_hostname)
            {
                std::cout << "MQTT SSL: Hostname verification disabled" << std::endl;
            }
            
            m_conn_opts.set_ssl(ssl_opts);
            std::cout << "MQTT SSL options configured successfully" << std::endl;
        }

        // Set Last Will and Testament (LWT) for offline detection
        std::string lwt_topic = buildTopic("lwt");
        Json::Value lwt_payload;
        lwt_payload["status"] = "offline";
        lwt_payload["timestamp"] = getCurrentTimestamp();
        lwt_payload["reflector_id"] = m_reflector_id;
        lwt_payload["reason"] = "svxreflector disconnected";

        Json::FastWriter writer;
        std::string lwt_message = writer.write(lwt_payload);

        mqtt::will_options will_opts(lwt_topic, lwt_message, 1, false);
        m_conn_opts.set_will(will_opts);

        // Set callbacks
        m_client->set_callback(*this);

        return connect();
    }
    catch (const mqtt::exception &exc)
    {
        std::cerr << "*** ERROR: MQTT initialization failed: " << exc.what() << std::endl;
        return false;
    }
}

bool MqttHandler::reconnect()
{
    if (m_initialized)
    {
        std::cout << "MQTT already initialized, skipping reconnect" << std::endl;
        return true;
    }

    return connect();
}

void MqttHandler::publishNodeEvent(const std::string &event_type,
                                   const std::string &callsign,
                                   Json::Value& data)
{
    try
    {
        data["timestamp"] = getCurrentTimestamp();

        Json::FastWriter writer;
        std::string payload = writer.write(data);
        publishTopic(buildTopic("nodes/" + callsign + "/" + event_type), payload, 0);
    }
    catch (const mqtt::exception &exc)
    {
        std::cerr << "*** ERROR: MQTT node event publish failed: " << exc.what() << std::endl;
    }
}

void MqttHandler::publishNodes(Json::Value& data)
{
    try
    {
        data["timestamp"] = getCurrentTimestamp();

        Json::FastWriter writer;
        std::string payload = writer.write(data);
        publishTopic(buildTopic("nodes/connected"), payload, 0);
    }
    catch (const mqtt::exception &exc)
    {
        std::cerr << "*** ERROR: MQTT nodes publish failed: " << exc.what() << std::endl;
    }
}

void MqttHandler::publishSystemEvent(const std::string &event_type,
                                     Json::Value& data)
{
    try
    {
        data["timestamp"] = getCurrentTimestamp();

        Json::FastWriter writer;
        std::string payload = writer.write(data);
        
        publishTopic(buildTopic("system/" + event_type), payload, 0);
    }
    catch (const mqtt::exception &exc)
    {
        std::cerr << "*** ERROR: MQTT system event publish failed: " << exc.what() << std::endl;
    }
}

void MqttHandler::publishCommandReply(Json::Value& data)
{
    try
    {
        data["timestamp"] = getCurrentTimestamp();

        Json::FastWriter writer;
        std::string payload = writer.write(data);
        publishTopic(buildTopic("commands/reply"), payload, 0);
    }
    catch (const mqtt::exception &exc)
    {
        std::cerr << "*** ERROR: MQTT command reply publish failed: " << exc.what() << std::endl;
    }
}

void MqttHandler::publishHeartbeat(int uptime_seconds)
{
    try
    {
        Json::Value heartbeat_payload;
        heartbeat_payload["status"] = "online";
        heartbeat_payload["timestamp"] = getCurrentTimestamp();
        heartbeat_payload["reflector_id"] = m_reflector_id;
        heartbeat_payload["uptime"] = uptime_seconds;
        heartbeat_payload["type"] = "heartbeat";

        Json::FastWriter writer;
        std::string heartbeat_message = writer.write(heartbeat_payload);

        publishTopic(buildTopic("lwt"), heartbeat_message, 0);
    }
    catch (const mqtt::exception &exc)
    {
        std::cerr << "*** ERROR: Failed to publish heartbeat: " << exc.what() << std::endl;
    }
}

bool MqttHandler::isConnected() const
{
    return (m_initialized && m_client && m_client->is_connected());
}

bool MqttHandler::isConnectedSafe() const
{
    return m_connected_state.load();
}


/****************************************************************************
 *
 * Protected member functions
 *
 ****************************************************************************/

 void MqttHandler::message_arrived(mqtt::const_message_ptr msg)
 {
    /* debug */
    /*
     std::cout << "=== MQTT MESSAGE ARRIVED ===" << std::endl;
     std::cout << "Topic: " << msg->get_topic() << std::endl;
     std::cout << "Payload: " << msg->get_payload_str() << std::endl;
     std::cout << "QoS: " << msg->get_qos() << std::endl;
     std::cout << "Retained: " << (msg->is_retained() ? "YES" : "NO") << std::endl;
     std::cout << "Payload length: " << msg->get_payload().length() << std::endl;
     std::cout << "===========================" << std::endl;
    */

     try
     {
         std::string payload = msg->get_payload_str();
         std::string topic = msg->get_topic();
 
         // Parse command message
         Json::Reader reader;
         Json::Value command;
         if (reader.parse(payload, command))
         {
             if (command.isMember("command") && m_command_callback)
             {
                 std::string cmd = command["command"].asString();
                 m_command_callback(cmd);
             }
             else
                 std::cout << "JSON message received but no command found" << std::endl;
         }
         else
         {
             if (m_command_callback)
                 m_command_callback(payload);
             else
                 std::cout << "No command callback set!" << std::endl;
         }
     }
     catch (const std::exception &e)
     {
         std::cerr << "*** ERROR: Failed to process MQTT message: " << e.what() << std::endl;
     }
 }
 
 void MqttHandler::connected(const std::string& cause)
 {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::cout << "MQTT Connected: " << cause << std::endl;
    m_connected_state.store(true);
    m_initialized = true;
 }

 void MqttHandler::connection_lost(const std::string& cause)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::cout << "MQTT Connection lost: " << cause << std::endl;
    m_connected_state.store(false);
}

void MqttHandler::delivery_complete(mqtt::delivery_token_ptr token)
{
    std::cout << "Delivery complete for token: " << token->get_message_id() << std::endl;
}

/****************************************************************************
 *
 * Private member functions
 *
 ****************************************************************************/

bool MqttHandler::connect()
{
    // Connect to broker
    mqtt::token_ptr conntok = m_client->connect(m_conn_opts);

    // Wait for connection to complete with timeout (don't hang forever)
    try
    {
        conntok->wait_for(std::chrono::seconds(30)); // 30 second timeout
    }
    catch (const std::exception &e)
    {
        std::cerr << "*** ERROR: MQTT connection timeout or failed: " << e.what() << std::endl;
        return false;
    }

    if (!conntok->is_complete())
    {
        std::cerr << "*** ERROR: MQTT connection failed" << std::endl;
        return false;
    }

    if (!m_client->is_connected())
    {
        std::cerr << "*** ERROR: MQTT connection failed" << std::endl;
        return false;
    }

    m_initialized = true;

    // Subscribe to commands
    subscribeToCommands();

    std::cout << "MQTT handler initialized successfully for reflector " << m_reflector_id << std::endl;
    return true;
}

void MqttHandler::subscribeToCommands()
{
    if (!m_client)
    {
        std::cout << "MQTT client not initialized, skipping subscription" << std::endl;
        return;
    }

    try
    {
        std::string topic = buildTopic("commands");
        mqtt::token_ptr subtok = m_client->subscribe(topic, 2);

        if (!subtok->wait_for(std::chrono::seconds(5)))
        {
            std::cout << "Subscription timed out after 5 seconds" << std::endl;
            return;
        }

        std::cout << "MQTT subscription process completed for topic: " << topic << std::endl;
    }
    catch (const mqtt::exception &exc)
    {
        std::cerr << "*** ERROR: MQTT subscription failed: " << exc.what() << std::endl;
        std::cerr << "*** ERROR: MQTT error code: " << exc.get_return_code() << std::endl;
    }
    catch (const std::exception &exc)
    {
        std::cerr << "*** ERROR: General subscription error: " << exc.what() << std::endl;
    }
}

void MqttHandler::publishTopic(const std::string& topic, const std::string& data, int qos)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_initialized || !m_connected_state.load())
        return;

    try
    {
        mqtt::message_ptr pubmsg = mqtt::make_message(topic, data, qos, false);
        m_client->publish(pubmsg);
    }
    catch (const mqtt::exception &exc)
    {
        std::cerr << "*** ERROR: MQTT publish failed: " << exc.what() << std::endl;
    }
}

std::string MqttHandler::buildTopic(const std::string &topic_type) const
{
    return m_topic_prefix + "/" + m_reflector_id + "/" + topic_type;
}

std::string MqttHandler::getCurrentTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
              1000;

    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return ss.str();
}


/*
 * This file has not been truncated
 */
