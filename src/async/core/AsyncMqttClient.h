/**
@file	 AsuncMqttClient.h
@brief   Async wrapper for Paho MQTT C++ client
@author  Rui Barreiros / CR7BPM
@date	 2026-01-07

\verbatim
SvxReflector - An audio reflector for connecting SvxLink Servers
Copyright (C) 2003-2026 Tobias Blomberg / SM0SVX

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

#ifndef ASYNC_MQTT_CLIENT_INCLUDED
#define ASYNC_MQTT_CLIENT_INCLUDED

#include <string>
#include <memory>
#include <mutex>
#include <queue>
#include <functional>
#include <sigc++/sigc++.h>
#include <mqtt/async_client.h>
#include <AsyncFdWatch.h>
#include <AsyncTimer.h>

namespace Async
{

/**
 * @brief Asynchronous MQTT client wrapper
 *
 * This class wraps the Paho MQTT C++ client (mqtt::async_client) and integrates
 * it with the Async library's event loop. 
 *
 * It handles the threading differences by queueing callbacks from Paho's 
 * background threads and processing them in the main Async event loop via 
 * a pipe and FdWatch.
 */
class MqttClient : public sigc::trackable,
                   public virtual mqtt::callback,
                   public virtual mqtt::iaction_listener
{
  public:
    /**
     * @brief Constructor
     * @param server_uri The URI of the MQTT broker (e.g., "tcp://localhost:1883")
     * @param client_id The client ID to use
     */
    MqttClient(const std::string& server_uri, const std::string& client_id);

    /**
     * @brief Destructor
     */
    virtual ~MqttClient();

    /**
     * @brief Connect to the MQTT broker
     * @param clean_session Whether to start a clean session
     */
    void connect(bool clean_session = true);

    /**
     * @brief Connect with specific options
     * @param opts Connection options
     */
    void connect(mqtt::connect_options& opts);

    /**
     * @brief Reconnect using the previously set options
     */
    void reconnect();

    /**
     * @brief Disconnect from the MQTT broker
     * @param timeout_ms Timeout for disconnect in milliseconds
     */
    void disconnect(long timeout_ms = 10000);

    /**
     * @brief Publish a message
     * @param topic The topic to publish to
     * @param payload The message payload
     * @param qos Quality of Service (0, 1, or 2)
     * @param retained Whether the message should be retained
     */
    void publish(const std::string& topic, const std::string& payload, int qos = 0, bool retained = false);

    /**
     * @brief Publish a Paho message object
     * @param msg The message to publish
     */
    void publish(mqtt::const_message_ptr msg);

    /**
     * @brief Subscribe to a topic
     * @param topic The topic to subscribe to
     * @param qos Quality of Service
     */
    void subscribe(const std::string& topic, int qos = 0);

    /**
     * @brief Unsubscribe from a topic
     * @param topic The topic to unsubscribe from
     */
    void unsubscribe(const std::string& topic);

    /**
     * @brief Set SSL/TLS options
     * @param ssl_opts SSL options
     */
    void setSslOptions(const mqtt::ssl_options& ssl_opts);

    /**
     * @brief Set the Last Will and Testament (LWT)
     * @param topic The topic to publish the will to
     * @param payload The message payload
     * @param qos Quality of Service (0, 1, or 2)
     * @param retained Whether the message should be retained
     */
    void setWill(const std::string& topic, const std::string& payload, int qos = 1, bool retained = true);

    /**
     * @brief Check if connected
     * @return true if connected, false otherwise
     */
    bool isConnected() const;

    // Signals
    // Signals
    sigc::signal<void()> signalConnected;
    sigc::signal<void(const std::string&)> disconnected; // reason
    sigc::signal<void(mqtt::const_message_ptr)> messageReceived;
    sigc::signal<void(mqtt::delivery_token_ptr)> published;
    sigc::signal<void(const std::string&)> subscribed; // topic
    sigc::signal<void(const std::string&)> error; // error message

  protected:
    // mqtt::callback implementation
    void connected(const std::string& cause) override;
    void connection_lost(const std::string& cause) override;
    void message_arrived(mqtt::const_message_ptr msg) override;
    void delivery_complete(mqtt::delivery_token_ptr token) override;

    // mqtt::iaction_listener implementation
    void on_failure(const mqtt::token& tok) override;
    void on_success(const mqtt::token& tok) override;

  private:
    std::shared_ptr<mqtt::async_client> client_;
    mqtt::connect_options conn_opts_;
    mqtt::ssl_options ssl_opts_;
    mqtt::will_options will_opts_;
    bool has_will_ = false;
    
    // Thread-safe event queueing via self-pipe pattern
    // The pipe bridges Paho's background threads with the main event loop
    int pipe_fds_[2]; // [0] = read, [1] = write
    FdWatch pipe_watch_;
    std::mutex queue_mutex_;
    std::queue<std::function<void()>> event_queue_;

    void queueEvent(std::function<void()> event);
    void processEvents(FdWatch* w);
    void setupPipe();
    void closePipe();
    void notifyEvent();

    bool ssl_enabled_ = false;

};

} // namespace Async

#endif // ASYNC_MQTT_CLIENT_INCLUDED
