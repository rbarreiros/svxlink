/**
 * @file   AsyncConfigBackend.cpp
 * @brief  Implementation of ConfigBackend base class and factory functions
 * @author Rui Barreiros
 * @date   2025-09-19

This file contains the base class implementation for configuration backends that
can load configuration data from various sources like files, databases, etc.

\verbatim
Async - A library for programming event driven applications
Copyright (C) 2004-2025 Tobias Blomberg / SM0SVX

This program is free software; you can redistribute  it and/or modify
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

#include "AsyncConfigBackend.h"
#include "AsyncConfigSource.h"
#include "AsyncTimer.h"
#include <iostream>

namespace Async
{

ConfigBackend::ConfigBackend(bool enable_notifications, unsigned int auto_poll_interval_ms)
  : m_enable_change_notifications(enable_notifications),
    m_default_poll_interval(auto_poll_interval_ms),
    m_current_poll_interval(0),
    m_poll_timer(nullptr)
{
  // Auto-start polling if notifications are enabled and interval > 0
  if (m_enable_change_notifications && auto_poll_interval_ms > 0)
  {
    startAutoPolling(auto_poll_interval_ms);
  }
}

ConfigBackend::~ConfigBackend(void)
{
  stopAutoPolling();
}

void ConfigBackend::setTablePrefix(const std::string& prefix)
{
  m_table_prefix = prefix;
}

std::string ConfigBackend::getFullTableName(const std::string& base_name) const
{
  return m_table_prefix + base_name;
}

void ConfigBackend::enableChangeNotifications(bool enable)
{
  m_enable_change_notifications = enable;
}

bool ConfigBackend::changeNotificationsEnabled(void) const
{
  return m_enable_change_notifications;
}

bool ConfigBackend::checkForExternalChanges(void)
{
  // Database backends should override this
  return false;
}

void ConfigBackend::startAutoPolling(unsigned int interval_ms)
{
  if (interval_ms == 0)
  {
    stopAutoPolling();
    return;
  }

  std::cout << "Starting auto-polling with interval: " 
    << interval_ms << " milliseconds" << std::endl;

  m_current_poll_interval = interval_ms;
  // Always make sure the cleanup is done first!!
  stopAutoPolling();
  
  m_poll_timer = new Async::Timer(interval_ms, Async::Timer::TYPE_PERIODIC, true);
  m_poll_timer->expired.connect(sigc::mem_fun(*this, &ConfigBackend::onPollTimer));
}

void ConfigBackend::stopAutoPolling(void)
{
  if (m_poll_timer != nullptr)
  {
    std::cout << "Stopping auto-polling" << std::endl;
    delete m_poll_timer;
    m_poll_timer = nullptr;
    m_current_poll_interval = 0;
  }
}

bool ConfigBackend::isAutoPolling(void) const
{
  return (m_poll_timer != nullptr);
}

unsigned int ConfigBackend::getPollingInterval(void) const
{
  return m_current_poll_interval;
}

void ConfigBackend::onPollTimer(Async::Timer* timer)
{
  //std::cout << "Checking for external changes" << " -- next check in " << timer->timeout() << " milliseconds" << std::endl;
  checkForExternalChanges();
}

void ConfigBackend::notifyValueChanged(const std::string& section,
                                      const std::string& tag,
                                      const std::string& value)
{
  if (m_enable_change_notifications)
  {
    std::cout << "Configuration changed: [" << section << "]/" << tag << " = " << value << std::endl;
    valueChanged(section, tag, value);
  }
}

// Factory convenience functions

ConfigBackendPtr createConfigBackend(const std::string& url)
{
  auto parsed = ConfigSource::parse(url);
  if (!parsed)
  {
    std::cerr << "*** ERROR: Invalid configuration source URL: " << url << std::endl;
    return nullptr;
  }

  return createConfigBackendByType(parsed->backend_type_name, parsed->connection_info);
}

ConfigBackendPtr createConfigBackendByType(const std::string& backend_type,
                                          const std::string& connection_info)
{
  ConfigBackend* backend = ConfigBackendFactory::createNamedObject(backend_type);
  if (!backend)
  {
    std::cerr << "*** ERROR: Failed to create backend of type '" << backend_type 
              << "'. Available: " << ConfigSource::availableBackendsString() << std::endl;
    return nullptr;
  }
  
  // Open the backend with the connection info
  if (!backend->open(connection_info))
  {
    std::cerr << "*** ERROR: Failed to open backend of type '" << backend_type 
              << "' with connection info: " << connection_info << std::endl;
    delete backend;
    return nullptr;
  }
  
  return ConfigBackendPtr(backend);
}

} // namespace Async

