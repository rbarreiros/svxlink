/**
@file	 MqttHandler.h
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

#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <string>
#include <memory>
#include <functional>
#include <mqtt/async_client.h>
#include <mqtt/connect_options.h>

class Reflector;

class MqttHandler : public mqtt::callback
{
public:
    MqttHandler(Reflector* reflector);
    ~MqttHandler();

    // Initialize MQTT connection
    bool init(const std::string& broker_host, int broker_port,
              const std::string& username, const std::string& password,
              const std::string& reflector_id, const std::string& topic_prefix);

    // Publish status updates
    void publishStatus(const std::string& status_json);

    // Publish node events
    void publishNodeEvent(const std::string& event_type, 
                         const std::string& callsign,
                         const std::string& event_data = "");

    // Publish system events
    void publishSystemEvent(const std::string& event_type,
                           const std::string& event_data = "");

    // Publish heartbeat/uptime update
    void publishHeartbeat(int uptime_seconds);

    // Check if MQTT is connected
    bool isConnected() const;

    // Get reflector ID
    const std::string& getReflectorId() const { return m_reflector_id; }

    // Set command callback
    void setCommandCallback(std::function<void(const std::string&)> callback) {
        m_command_callback = callback;
    }

private:
    Reflector* m_reflector;
    std::string m_reflector_id;
    std::string m_topic_prefix;
    std::function<void(const std::string&)> m_command_callback;

    std::unique_ptr<mqtt::async_client> m_client;
    mqtt::connect_options m_conn_opts;
    
    // MQTT message handlers
    void onMessage(const mqtt::const_message_ptr& msg);
    void onConnect(const mqtt::token& tok);
    void onDisconnect(const mqtt::token& tok);
    void onConnectionLost(const std::string& cause);
    
    // Helper methods
    void subscribeToCommands();
    std::string buildTopic(const std::string& topic_type) const;
    std::string getCurrentTimestamp();

    bool m_initialized;
};

#endif // MQTT_HANDLER_H 