/**
@file	 AsuncMqttClient.cpp
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

#include "AsyncMqttClient.h"
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <cassert>

using namespace std;

namespace Async
{

MqttClient::MqttClient(const std::string& server_uri, const std::string& client_id)
  : client_(make_shared<mqtt::async_client>(server_uri, client_id)),
    pipe_watch_(),
    ssl_enabled_(false)
{
  setupPipe();
  pipe_watch_.activity.connect(mem_fun(*this, &MqttClient::processEvents));
  
  client_->set_callback(*this);
}

MqttClient::~MqttClient()
{
  try {
    if (client_->is_connected()) {
      client_->disconnect()->wait();
    }
  } catch (...) {
    // Ignore errors on destruction
  }
  closePipe();
}

void MqttClient::setupPipe()
{
  if (pipe(pipe_fds_) == -1) {
    perror("pipe");
    throw std::runtime_error("Failed to create pipe for AsyncMqttClient");
  }
  
  // Set non-blocking
  fcntl(pipe_fds_[0], F_SETFL, O_NONBLOCK);
  fcntl(pipe_fds_[1], F_SETFL, O_NONBLOCK);

  pipe_watch_.setFd(pipe_fds_[0], FdWatch::FD_WATCH_RD);
  pipe_watch_.setEnabled(true);
}

void MqttClient::closePipe()
{
  pipe_watch_.setEnabled(false);
  if (pipe_fds_[0] != -1) {
    close(pipe_fds_[0]);
    pipe_fds_[0] = -1;
  }
  if (pipe_fds_[1] != -1) {
    close(pipe_fds_[1]);
    pipe_fds_[1] = -1;
  }
}

void MqttClient::queueEvent(std::function<void()> event)
{
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    event_queue_.push(event);
  }
  notifyEvent();
}

void MqttClient::notifyEvent()
{
  // Write a single byte to wake up the event loop
  // EAGAIN/EWOULDBLOCK means pipe buffer is full, which is fine - 
  // the event loop will drain it and process all queued events
  char dummy = 'x';
  ssize_t result = write(pipe_fds_[1], &dummy, 1);
  if (result == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
    perror("write to pipe");
  }
}

void MqttClient::processEvents(FdWatch* w)
{
  char buf[32];
  while (read(pipe_fds_[0], buf, sizeof(buf)) > 0) {
    // drain pipe
  }

  std::queue<std::function<void()>> processing_queue;
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    processing_queue.swap(event_queue_);
  }

  while (!processing_queue.empty()) {
    try {
      processing_queue.front()();
    } catch (const std::exception& e) {
      cerr << "Exception in AsyncMqttClient event handler: " << e.what() << endl;
    } catch (...) {
      cerr << "Unknown exception in AsyncMqttClient event handler" << endl;
    }
    processing_queue.pop();
  }
}

void MqttClient::connect(bool clean_session)
{
  conn_opts_ = mqtt::connect_options();
  conn_opts_.set_clean_session(clean_session);
  // conn_opts_.set_automatic_reconnect(true); // Let user handle reconnects or Paho? 
  // Paho's auto reconnect might trigger callback confusion if not careful, but let's stick to manual or simple for now.
  
  // If SSL options were set, apply them
  if (ssl_enabled_) {
     conn_opts_.set_ssl(ssl_opts_); 
  }

  try {
    client_->connect(conn_opts_, nullptr, *this);
  } catch (const mqtt::exception& exc) {
    string err_msg = string("Connection error: ") + exc.what();
    cerr << err_msg << endl;
    queueEvent([this, err_msg]() {
      this->error.emit(err_msg);
    });
  }
}

void MqttClient::connect(mqtt::connect_options& opts)
{
  conn_opts_ = opts;
  if (has_will_) {
    conn_opts_.set_will(will_opts_);
  }
  try {
    client_->connect(conn_opts_, nullptr, *this);
  } catch (const mqtt::exception& exc) {
    string err_msg = string("Connection error: ") + exc.what();
    cerr << err_msg << endl;
    queueEvent([this, err_msg]() {
      this->error.emit(err_msg);
    });
  }
}

void MqttClient::reconnect()
{
  try {
    client_->connect(conn_opts_, nullptr, *this);
  } catch (const mqtt::exception& exc) {
    string err_msg = string("Reconnection error: ") + exc.what();
    cerr << err_msg << endl;
    queueEvent([this, err_msg]() {
      this->error.emit(err_msg);
    });
  }
}

void MqttClient::disconnect(long timeout_ms)
{
  try {
    client_->disconnect(timeout_ms)->wait();
  } catch (...) {}
}

void MqttClient::publish(const std::string& topic, const std::string& payload, int qos, bool retained)
{
  try {
    client_->publish(topic, payload, qos, retained);
  } catch (const mqtt::exception& exc) {
    string err_msg = string("Publish error: ") + exc.what();
    cerr << err_msg << endl;
    queueEvent([this, err_msg]() {
      this->error.emit(err_msg);
    });
  }
}

void MqttClient::publish(mqtt::const_message_ptr msg)
{
  try {
    client_->publish(msg);
  } catch (const mqtt::exception& exc) {
    string err_msg = string("Publish error: ") + exc.what();
    cerr << err_msg << endl;
    queueEvent([this, err_msg]() {
      this->error.emit(err_msg);
    });
  }
}

void MqttClient::subscribe(const std::string& topic, int qos)
{
  try {
    client_->subscribe(topic, qos, nullptr, *this);
  } catch (const mqtt::exception& exc) {
    string err_msg = string("Subscribe error: ") + exc.what();
    cerr << err_msg << endl;
    queueEvent([this, err_msg]() {
      this->error.emit(err_msg);
    });
  }
}

void MqttClient::unsubscribe(const std::string& topic)
{
  try {
    client_->unsubscribe(topic)->wait();
  } catch (const mqtt::exception& exc) {
    string err_msg = string("Unsubscribe error: ") + exc.what();
    cerr << err_msg << endl;
    queueEvent([this, err_msg]() {
      this->error.emit(err_msg);
    });
  }
}

void MqttClient::setSslOptions(const mqtt::ssl_options& ssl_opts)
{
  ssl_opts_ = ssl_opts;
  ssl_enabled_ = true;
}

void MqttClient::setWill(const std::string& topic, const std::string& payload, int qos, bool retained)
{
  will_opts_ = mqtt::will_options(topic, payload, qos, retained);
  has_will_ = true;
}

bool MqttClient::isConnected() const
{
  return client_->is_connected();
}

// -----------------------------------------------------------
// Callbacks (run on Paho background threads)
// -----------------------------------------------------------

void MqttClient::connected(const std::string& cause)
{
  // This is Paho's "connected" callback, but often on_success is used for the async connect token.
  // However, `mqtt::callback::connected` is called when reconnection happens.
  // We'll treat it as a connection event.
  queueEvent([this]() {
     this->signalConnected.emit();
  });
}

void MqttClient::connection_lost(const std::string& cause)
{
  string c = cause;
  queueEvent([this, c]() {
    this->disconnected.emit(c);
  });
}

void MqttClient::message_arrived(mqtt::const_message_ptr msg)
{
  queueEvent([this, msg]() {
    this->messageReceived.emit(msg);
  });
}

void MqttClient::delivery_complete(mqtt::delivery_token_ptr token)
{
  queueEvent([this, token]() {
    this->published.emit(token);
  });
}

// -----------------------------------------------------------
// IActionListener Callbacks (for async Connect/Subscribe)
// -----------------------------------------------------------

void MqttClient::on_failure(const mqtt::token& tok)
{
   // Emit error signal for async operation failures
   int msg_id = tok.get_message_id();
   string err_msg = string("MQTT operation failed for message ID: ") + std::to_string(msg_id);
   
   queueEvent([this, err_msg]() {
      this->error.emit(err_msg);
   });
}

void MqttClient::on_success(const mqtt::token& tok) 
{
    // on_success is called for connect, subscribe, publish (if async).
    // For connection, we rely on the connected() callback which handles
    // both initial connection and automatic reconnections.
}

} // namespace Async
