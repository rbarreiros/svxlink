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