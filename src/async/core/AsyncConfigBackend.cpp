/**
 * @file   AsyncConfigBackend.cpp
 * @brief  Implementation of ConfigBackend base class and factory functions
 * @author Tobias Blomberg / SM0SVX & Ricardo Barreiros / CT7ALW
 * @date   2025-01-XX
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
  // Default implementation does nothing
  // Database backends override this
  return false;
}

void ConfigBackend::startAutoPolling(unsigned int interval_ms)
{
  if (interval_ms == 0)
  {
    stopAutoPolling();
    return;
  }

  stopAutoPolling();
  m_poll_timer = new Async::Timer(interval_ms);
  m_poll_timer->expired.connect(sigc::mem_fun(*this, &ConfigBackend::onPollTimer));
  m_poll_timer->setEnable(true);
}

void ConfigBackend::stopAutoPolling(void)
{
  if (m_poll_timer != nullptr)
  {
    delete m_poll_timer;
    m_poll_timer = nullptr;
  }
}

bool ConfigBackend::isAutoPolling(void) const
{
  return (m_poll_timer != nullptr);
}

void ConfigBackend::onPollTimer(Async::Timer* timer)
{
  checkForExternalChanges();
}

void ConfigBackend::notifyValueChanged(const std::string& section,
                                      const std::string& tag,
                                      const std::string& value)
{
  if (m_enable_change_notifications)
  {
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

