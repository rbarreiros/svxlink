/**
@file	 MqttHandler.cpp
@brief   MQTT handler class
@author  Rui Barreiros / CR7BPM
@date	 2025-08-05

\verbatim
Copyright (C) 2025 Rui Barreiros / CR7BPM

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

#include "MqttHandler.h"
#include "Reflector.h"
#include <iostream>
#include <sstream>
#include <json/json.h>

MqttHandler::MqttHandler(Reflector* reflector)
    : m_reflector(reflector), m_initialized(false)
{
}

MqttHandler::~MqttHandler()
{
    if (m_client && m_client->is_connected())
    {
        // Publish offline status before disconnecting
        Json::Value status_payload;
        status_payload["status"] = "offline";
        status_payload["timestamp"] = getCurrentTimestamp();
        status_payload["reflector_id"] = m_reflector_id;
        status_payload["reason"] = "shutdown";
        
        Json::FastWriter writer;
        std::string status_message = writer.write(status_payload);
        
        try
        {
            std::string status_topic = buildTopic("status");
            m_client->publish(status_topic, status_message, 1, false);
            std::cout << "MQTT offline status published to " << status_topic << std::endl;
        }
        catch (const mqtt::exception& exc)
        {
            std::cerr << "*** ERROR: Failed to publish offline status: " << exc.what() << std::endl;
        }
        
        m_client->disconnect();
    }
}

bool MqttHandler::init(const std::string& broker_host, int broker_port,
                       const std::string& username, const std::string& password,
                       const std::string& reflector_id, const std::string& topic_prefix)
{
        try
    {
        m_reflector_id = reflector_id;
        m_topic_prefix = topic_prefix;

        // Create client
        std::string server_uri = "tcp://" + broker_host + ":" + std::to_string(broker_port);
        m_client = std::unique_ptr<mqtt::async_client>(new mqtt::async_client(server_uri, reflector_id));
        
        // Set connection options
        m_conn_opts.set_keep_alive_interval(60);
        m_conn_opts.set_clean_session(true);
        m_conn_opts.set_user_name(username);
        m_conn_opts.set_password(password);
        
        // Set Last Will and Testament (LWT) for offline detection
        std::string lwt_topic = buildTopic("status");
        Json::Value lwt_payload;
        lwt_payload["status"] = "offline";
        lwt_payload["timestamp"] = getCurrentTimestamp();
        lwt_payload["reflector_id"] = m_reflector_id;
        
        Json::FastWriter writer;
        std::string lwt_message = writer.write(lwt_payload);
        
        mqtt::will_options will_opts(lwt_topic, lwt_message, 1, false);
        m_conn_opts.set_will(will_opts);
        
        // Set callbacks
        m_client->set_callback(*this);
        
        // Connect to broker
        mqtt::token_ptr conntok = m_client->connect(m_conn_opts);
        conntok->wait();
        
        if (conntok->get_return_code() != 0)
        {
            std::cerr << "*** ERROR: MQTT connection failed with return code "
                      << conntok->get_return_code() << std::endl;
            return false;
        }
        
        // Subscribe to commands
        subscribeToCommands();
        
        m_initialized = true;
        std::cout << "MQTT handler initialized for reflector " << reflector_id << std::endl;
        return true;
    }
    catch (const mqtt::exception& exc)
    {
        std::cerr << "*** ERROR: MQTT initialization failed: " << exc.what() << std::endl;
        return false;
    }
}

void MqttHandler::publishStatus(const std::string& status_json)
{
    if (!m_initialized || !m_client || !m_client->is_connected())
    {
        return;
    }
    
    try
    {
        // Create message with timestamp
        Json::Value message;
        message["data"] = Json::Value(status_json);
        message["timestamp"] = getCurrentTimestamp();
        
        Json::FastWriter writer;
        std::string payload = writer.write(message);
        
        std::string topic = buildTopic("status");
        mqtt::message_ptr pubmsg = mqtt::make_message(topic, payload);
        pubmsg->set_qos(1);
        
        m_client->publish(pubmsg);
    }
    catch (const mqtt::exception& exc)
    {
        std::cerr << "*** ERROR: MQTT status publish failed: " << exc.what() << std::endl;
    }
}

void MqttHandler::publishNodeEvent(const std::string& event_type, 
                                  const std::string& callsign,
                                  const std::string& event_data)
{
    if (!m_initialized || !m_client || !m_client->is_connected())
    {
        return;
    }
    
    try
    {
        Json::Value message;
        Json::Value data;
        data["callsign"] = callsign;
        data["timestamp"] = getCurrentTimestamp();
        
        if (!event_data.empty())
        {
            // Try to parse event_data as JSON, otherwise use as string
            Json::Reader reader;
            Json::Value parsed_data;
            if (reader.parse(event_data, parsed_data))
            {
                data["data"] = parsed_data;
            }
            else
            {
                data["data"] = event_data;
            }
        }
        
        message["data"] = data;
        message["timestamp"] = getCurrentTimestamp();
        
        Json::FastWriter writer;
        std::string payload = writer.write(message);
        
        std::string topic = buildTopic("nodes/" + event_type);
        mqtt::message_ptr pubmsg = mqtt::make_message(topic, payload);
        pubmsg->set_qos(0); // Fire and forget for events
        
        m_client->publish(pubmsg);
    }
    catch (const mqtt::exception& exc)
    {
        std::cerr << "*** ERROR: MQTT node event publish failed: " << exc.what() << std::endl;
    }
}

void MqttHandler::publishSystemEvent(const std::string& event_type,
                                    const std::string& event_data)
{
    if (!m_initialized || !m_client || !m_client->is_connected())
    {
        return;
    }
    
    try
    {
        Json::Value message;
        Json::Value data;
        data["event_type"] = event_type;
        data["timestamp"] = getCurrentTimestamp();
        
        if (!event_data.empty())
        {
            Json::Reader reader;
            Json::Value parsed_data;
            if (reader.parse(event_data, parsed_data))
            {
                data["data"] = parsed_data;
            }
            else
            {
                data["data"] = event_data;
            }
        }
        
        message["data"] = data;
        message["timestamp"] = getCurrentTimestamp();
        
        Json::FastWriter writer;
        std::string payload = writer.write(message);
        
        std::string topic = buildTopic("events/" + event_type);
        mqtt::message_ptr pubmsg = mqtt::make_message(topic, payload);
        pubmsg->set_qos(0);
        
        m_client->publish(pubmsg);
    }
    catch (const mqtt::exception& exc)
    {
        std::cerr << "*** ERROR: MQTT system event publish failed: " << exc.what() << std::endl;
    }
}

void MqttHandler::publishHeartbeat(int uptime_seconds)
{
    if (!m_initialized || !m_client || !m_client->is_connected())
    {
        return;
    }

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

        std::string topic = buildTopic("status");
        
        // Use QoS 0 for heartbeats to avoid buffer overflow issues
        m_client->publish(topic, heartbeat_message, 0, false);
    }
    catch (const mqtt::exception& exc)
    {
        std::cerr << "*** ERROR: Failed to publish heartbeat: " << exc.what() << std::endl;
    }
}

bool MqttHandler::isConnected() const
{
    return m_initialized && m_client && m_client->is_connected();
}

void MqttHandler::onMessage(const mqtt::const_message_ptr& msg)
{
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
                std::cout << "MQTT command received: " << cmd << std::endl;
                m_command_callback(cmd);
            }
            else if (command.isMember("data") && command["data"].isString())
            {
                // Handle simple string commands (for backward compatibility)
                std::string cmd = command["data"].asString();
                std::cout << "MQTT command received (legacy format): " << cmd << std::endl;
                m_command_callback(cmd);
            }
        }
        else
        {
            // Handle plain text commands
            std::cout << "MQTT command received (plain text): " << payload << std::endl;
            m_command_callback(payload);
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "*** ERROR: Failed to process MQTT message: " << e.what() << std::endl;
    }
}

void MqttHandler::onConnect(const mqtt::token& tok)
{
    std::cout << "MQTT connected to broker" << std::endl;
    
    // Subscribe to commands
    subscribeToCommands();
    
    // Publish online status
    Json::Value status_payload;
    status_payload["status"] = "online";
    status_payload["timestamp"] = getCurrentTimestamp();
    status_payload["reflector_id"] = m_reflector_id;
    status_payload["version"] = "1.3.99.2";
    status_payload["uptime"] = 0;
    
    Json::FastWriter writer;
    std::string status_message = writer.write(status_payload);
    
    try
    {
        std::string status_topic = buildTopic("status");
        m_client->publish(status_topic, status_message, 1, false);
        std::cout << "MQTT online status published to " << status_topic << std::endl;
    }
    catch (const mqtt::exception& exc)
    {
        std::cerr << "*** ERROR: Failed to publish online status: " << exc.what() << std::endl;
    }
    
    // Publish startup event
    publishSystemEvent("startup", "Reflector started and connected to MQTT");
}

void MqttHandler::onDisconnect(const mqtt::token& tok)
{
    std::cout << "MQTT disconnected from broker" << std::endl;
}

void MqttHandler::onConnectionLost(const std::string& cause)
{
    std::cerr << "*** WARNING: MQTT connection lost: " << cause << std::endl;
}

void MqttHandler::subscribeToCommands()
{
    if (!m_client || !m_client->is_connected())
    {
        return;
    }
    
    try
    {
        std::string topic = buildTopic("commands");
        m_client->subscribe(topic, 2); // QoS 2 for commands
        
        std::cout << "MQTT subscribed to commands on topic: " << topic << std::endl;
    }
    catch (const mqtt::exception& exc)
    {
        std::cerr << "*** ERROR: MQTT subscription failed: " << exc.what() << std::endl;
    }
}

std::string MqttHandler::buildTopic(const std::string& topic_type) const
{
    return m_topic_prefix + "/" + m_reflector_id + "/" + topic_type;
}

std::string MqttHandler::getCurrentTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return ss.str();
} 