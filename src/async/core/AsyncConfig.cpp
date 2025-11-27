/**
@file	 AsyncConfig.cpp
@brief   A class for configuration handling
@author  Tobias Blomberg / SM0SVX
@date	 2004-03-17

This file contains a class that is used to supply and save configuration data
to a backend which can be a file, a database, etc. The backend can be
implemented by extending AsyncConfigBackend.

\include test.cfg

\verbatim
Async - A library for programming event driven applications
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



/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>

#include <iostream>
#include <cassert>
#include <cstring>
#include <vector>


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

#include "AsyncConfig.h"
#include "AsyncConfigBackend.h"
#include "AsyncConfigManager.h"



/****************************************************************************
 *
 * Namespaces to use
 *
 ****************************************************************************/

using namespace std;
using namespace Async;


/****************************************************************************
 *
 * Defines & typedefs
 *
 ****************************************************************************/

#ifndef SVX_SYSCONF_INSTALL_DIR
#define SVX_SYSCONF_INSTALL_DIR "/etc"
#endif


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
 * Prototypes
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
 * Public member functions
 *
 ****************************************************************************/


Config::~Config(void)
{
} /* Config::~Config */


bool Config::open(const string& config_dir)
{
  ConfigManager manager;

  // Initialize backend using db.conf with default config file name and table prefix
  m_backend = manager.initializeBackend(config_dir, "svxlink.conf", "svxlink_");
  if (m_backend == nullptr)
  {
    cerr << "*** FATAL ERROR: " << manager.getLastError() << endl;
    cerr << "*** APPLICATION ABORTING: Cannot initialize configuration backend" << endl;
    exit(1);  // Abort application as requested
  }

  // For CFG_DIR resolution, we need a reference point:
  // - For file backend: use the main config file path
  // - For database backend: use the db.conf location
  m_main_config_file = manager.getMainConfigReference();

  finalizeBackendSetup();
  return true;
} /* Config::open */

bool Config::openFromDbConfig(const string& db_conf_path)
{
  return openFromDbConfigInternal(db_conf_path, "svxlink.conf", "svxlink_", true);
} /* Config::openFromDbConfig */

bool Config::openFromDbConfigInternal(const std::string& db_conf_path,
                                       const std::string& default_config_name,
                                       const std::string& default_table_prefix,
                                       bool abort_on_failure)
{
  ConfigManager manager;

  // Initialize backend using specific db.conf file
  m_backend = manager.initializeBackendFromFile(db_conf_path, default_config_name, default_table_prefix);
  if (m_backend == nullptr)
  {
    if (abort_on_failure)
    {
      cerr << "*** FATAL ERROR: " << manager.getLastError() << endl;
      cerr << "*** APPLICATION ABORTING: Cannot initialize configuration backend from " << db_conf_path << endl;
      exit(1);
    }
    return false;
  }

  // For CFG_DIR resolution, use the db.conf location as reference
  m_main_config_file = manager.getMainConfigReference();

  finalizeBackendSetup();
  return true;
} /* Config::openFromDbConfigInternal */

bool Config::openDirect(const string& source)
{
  // Create the appropriate backend using the factory
  m_backend = createConfigBackend(source);
  if (m_backend == nullptr)
  {
    cerr << "*** ERROR: Failed to create configuration backend for: " << source << endl;
    return false;
  }

  // Store the main config file path for CFG_DIR resolution
  if (source.find("file://") == 0)
  {
    m_main_config_file = source.substr(7); // Remove "file://" prefix
  }
  else
  {
    m_main_config_file = ""; // Database backend - no single file
  }

  cout << "Using " << m_backend->getBackendType() << " configuration backend: " 
       << m_backend->getBackendInfo() << endl;

  finalizeBackendSetup();
  return true;
} /* Config::openDirect */


std::string Config::getMainConfigFile(void) const
{
  return m_main_config_file;
} /* Config::getMainConfigFile */

std::string Config::getBackendType(void) const
{
  if (m_backend != nullptr)
  {
    return m_backend->getBackendType();
  }
  return "";
} /* Config::getBackendType */


bool Config::getValue(const std::string& section, const std::string& tag,
                      std::string& value, bool missing_ok) const
{
  // First try to get from in-memory cache (for subscriptions)
  Sections::const_iterator sec_it = m_sections.find(section);
  if (sec_it != m_sections.end())
  {
    Values::const_iterator val_it = sec_it->second.find(tag);
    if (val_it != sec_it->second.end())
    {
      value = val_it->second.val;
      return true;
    }
  }

  // If not in cache, try to get from backend
  if (m_backend != nullptr && m_backend->isOpen())
  {
    if (m_backend->getValue(section, tag, value))
    {
      return true;
    }
  }

  return missing_ok;
} /* Config::getValue */


bool Config::getValue(const std::string& section, const std::string& tag,
                      char& value, bool missing_ok) const
{
  string str_value;
  if (!getValue(section, tag, str_value, missing_ok))
  {
    return missing_ok;
  }

  if (str_value.size() != 1)
  {
    return false;
  }

  value = str_value[0];
  return true;
} /* Config::getValue */


const string &Config::getValue(const string& section, const string& tag) const
{
  static string cached_value;
  
  if (getValue(section, tag, cached_value, true))
  {
    return cached_value;
  }
  
  static const string empty_string;
  return empty_string;
} /* Config::getValue */


list<string> Config::listSections(void)
{
  if (m_backend != nullptr && m_backend->isOpen())
  {
    return m_backend->listSections();
  }
  
  list<string> section_list;
  for (Sections::const_iterator it = m_sections.begin(); it != m_sections.end(); ++it)
  {
    section_list.push_back(it->first);
  }
  return section_list;
} /* Config::listSections */


list<string> Config::listSection(const string& section)
{
  if (m_backend != nullptr && m_backend->isOpen())
  {
    return m_backend->listSection(section);
  }
  
  list<string> tags;
  
  if (m_sections.count(section) == 0)
  {
    return tags;
  }
  
  const Values& values = m_sections.at(section);
  for (Values::const_iterator it = values.begin(); it != values.end(); ++it)
  {
    tags.push_back(it->first);
  }
  
  return tags;
} /* Config::listSection */


void Config::setValue(const std::string& section, const std::string& tag,
                      const std::string& value)
{
  // Update in-memory cache
  Values &values = m_sections[section];
  bool value_changed = (value != values[tag].val);
  
  if (value_changed)
  {
    values[tag].val = value;
    
    // Sync to backend
    syncToBackend(section, tag);
    
    // Emit signals and call subscribers
    valueUpdated(section, tag, value);
    for (const auto& func : values[tag].subs)
    {
      func(value);
    }
  }
} /* Config::setValue */


/****************************************************************************
 *
 * Protected member functions
 *
 ****************************************************************************/


/*
 *------------------------------------------------------------------------
 * Method:    
 * Purpose:   
 * Input:     
 * Output:    
 * Author:    
 * Created:   
 * Remarks:   
 * Bugs:      
 *------------------------------------------------------------------------
 */






/****************************************************************************
 *
 * Private member functions
 *
 ****************************************************************************/

void Config::loadFromBackend(void)
{
  if (m_backend == nullptr || !m_backend->isOpen())
  {
    return;
  }

  // Load all sections and their tags/values into memory cache
  list<string> sections = m_backend->listSections();
  for (const string& section : sections)
  {
    list<string> tags = m_backend->listSection(section);
    for (const string& tag : tags)
    {
      string value;
      if (m_backend->getValue(section, tag, value))
      {
        m_sections[section][tag].val = value;
      }
    }
  }
} /* Config::loadFromBackend */

void Config::syncToBackend(const std::string& section, const std::string& tag)
{
  if (m_backend == nullptr || !m_backend->isOpen())
  {
    return;
  }

  // Get the value from in-memory cache
  Sections::const_iterator sec_it = m_sections.find(section);
  if (sec_it != m_sections.end())
  {
    Values::const_iterator val_it = sec_it->second.find(tag);
    if (val_it != sec_it->second.end())
    {
      if (!m_backend->setValue(section, tag, val_it->second.val))
      {
        cerr << "*** WARNING: Failed to sync configuration change to backend: "
             << section << "/" << tag << endl;
      }
    }
  }
} /* Config::syncToBackend */

ConfigBackend* Config::getBackend(void)
{
  return m_backend.get();
} /* Config::getBackend */

void Config::reload(void)
{
  if (m_backend == nullptr || !m_backend->isOpen())
  {
    return;
  }

  // For database backends, check for external changes first
  if (m_backend->getBackendType() != "file")
  {
    m_backend->checkForExternalChanges();
  }

  // Reload all sections and tags
  auto sections = m_backend->listSections();
  for (const auto& section : sections)
  {
    auto tags = m_backend->listSection(section);
    for (const auto& tag : tags)
    {
      std::string new_value;
      if (m_backend->getValue(section, tag, new_value))
      {
        // Check if value changed
        auto sec_it = m_sections.find(section);
        if (sec_it != m_sections.end())
        {
          auto val_it = sec_it->second.find(tag);
          if (val_it != sec_it->second.end())
          {
            if (val_it->second.val != new_value)
            {
              // Value changed, update cache and trigger subscriptions
              std::string old_value = val_it->second.val;
              val_it->second.val = new_value;
              
              // Notify all subscribers
              for (auto& sub : val_it->second.subs)
              {
                sub(new_value);
              }
              
              // Emit valueUpdated signal
              valueUpdated(section, tag, new_value);
            }
          }
        }
      }
    }
  }
} /* Config::reload */

void Config::onBackendValueChanged(const std::string& section,
                                    const std::string& tag,
                                    const std::string& value)
{
  //std::cout << "[DEBUG Config] onBackendValueChanged called: [" << section << "]" << tag 
  //          << " = '" << value << "'" << std::endl;

  // Update in-memory cache
  m_sections[section][tag].val = value;

  // Trigger subscriptions
  auto sec_it = m_sections.find(section);
  if (sec_it != m_sections.end())
  {
    auto val_it = sec_it->second.find(tag);
    if (val_it != sec_it->second.end())
    {
      //std::cout << "[DEBUG Config] Found " << val_it->second.subs.size() 
      //          << " subscription(s) for [" << section << "]" << tag << std::endl;
      for (auto& sub : val_it->second.subs)
      {
        //std::cout << "[DEBUG Config] Triggering subscription callback" << std::endl;
        sub(value);
      }
    }
    else
    {
      //std::cout << "[DEBUG Config] Tag not found in cache: [" << section << "]" << tag << std::endl;
    }
  }
  else
  {
    //std::cout << "[DEBUG Config] Section not found in cache: [" << section << "]" << std::endl;
  }

  // Emit valueUpdated signal
  valueUpdated(section, tag, value);
} /* Config::onBackendValueChanged */

void Config::connectBackendSignals(void)
{
  if (m_backend != nullptr)
  {
    m_backend->valueChanged.connect(
        sigc::mem_fun(*this, &Config::onBackendValueChanged));
  }
} /* Config::connectBackendSignals */

void Config::finalizeBackendSetup(void)
{
  if (m_backend == nullptr)
  {
    return;
  }

  // Disable notifications during initial config load to avoid spurious signals
  bool was_enabled = m_backend->changeNotificationsEnabled();
  if (was_enabled)
  {
    m_backend->enableChangeNotifications(false);
  }

  // Load all configuration data into memory for subscription support
  loadFromBackend();

  // Re-enable notifications before connecting signals
  if (was_enabled)
  {
    m_backend->enableChangeNotifications(true);
  }

  // Connect backend signals for external change detection
  connectBackendSignals();
} /* Config::finalizeBackendSetup */

Config::ConfigLoadResult Config::openWithFallback(const std::string& cmdline_config,
                                                   const std::string& cmdline_dbconfig,
                                                   const std::string& default_config_name)
{
  ConfigLoadResult result;
  result.success = false;
  result.used_dbconfig = false;

  // Derive table prefix from config name
  // svxlink.conf → svxlink_
  // svxreflector.conf → svxreflector_
  // remotetrx.conf → remotetrx_
  std::string default_table_prefix;
  size_t dot_pos = default_config_name.find('.');
  if (dot_pos != std::string::npos)
  {
    std::string base_name = default_config_name.substr(0, dot_pos);
    default_table_prefix = base_name + "_";
  }

  // Priority 1: --config option
  if (!cmdline_config.empty())
  {
    if (openDirect("file://" + cmdline_config))
    {
      result.success = true;
      result.source_type = "command_line";
      result.source_path = cmdline_config;
      result.backend_type = getBackendType();
      result.used_dbconfig = false;
      return result;
    }
    else
    {
      result.error_message = "Failed to open configuration file: " + cmdline_config;
      return result;
    }
  }

  // Priority 2: --dbconfig option
  if (!cmdline_dbconfig.empty())
  {
    if (openFromDbConfigInternal(cmdline_dbconfig, default_config_name, default_table_prefix, false))
    {
      result.success = true;
      result.source_type = "command_line";
      result.source_path = cmdline_dbconfig;
      result.backend_type = getBackendType();
      result.used_dbconfig = true;
      return result;
    }
    else
    {
      result.error_message = "Failed to open database configuration: " + cmdline_dbconfig;
      return result;
    }
  }

  // Priority 3: Search for db.conf in standard locations
  std::vector<std::string> search_paths = {
    std::string(getenv("HOME") ? getenv("HOME") : "") + "/.svxlink/db.conf",
    "/etc/svxlink/db.conf",
    std::string(SVX_SYSCONF_INSTALL_DIR) + "/db.conf"
  };

  for (const auto& db_conf_path : search_paths)
  {
    struct stat st;
    if (stat(db_conf_path.c_str(), &st) == 0)
    {
      if (openFromDbConfigInternal(db_conf_path, default_config_name, default_table_prefix, false))
      {
        result.success = true;
        result.source_type = "dbconfig";
        result.source_path = db_conf_path;
        result.backend_type = getBackendType();
        result.used_dbconfig = true;
        return result;
      }
      else
      {
        result.error_message = "Found db.conf at " + db_conf_path + " but failed to load it";
        return result;
      }
    }
  }

  // Priority 4: Search for default config file in standard locations
  std::vector<std::string> config_search_paths = {
    std::string(getenv("HOME") ? getenv("HOME") : "") + "/.svxlink/" + default_config_name,
    "/etc/svxlink/" + default_config_name,
    std::string(SVX_SYSCONF_INSTALL_DIR) + "/" + default_config_name
  };

  for (const auto& config_path : config_search_paths)
  {
    struct stat st;
    if (stat(config_path.c_str(), &st) == 0)
    {
      if (openDirect("file://" + config_path))
      {
        result.success = true;
        result.source_type = "default";
        result.source_path = config_path;
        result.backend_type = getBackendType();
        result.used_dbconfig = false;
        return result;
      }
      else
      {
        result.error_message = "Found configuration at " + config_path + " but failed to load it";
        return result;
      }
    }
  }

  // Nothing found
  result.source_type = "none";
  result.error_message = "No configuration found. Searched for:\n";
  result.error_message += "  - db.conf in: ~/.svxlink/, /etc/svxlink/, " + std::string(SVX_SYSCONF_INSTALL_DIR) + "\n";
  result.error_message += "  - " + default_config_name + " in: ~/.svxlink/, /etc/svxlink/, " + std::string(SVX_SYSCONF_INSTALL_DIR);
  return result;
} /* Config::openWithFallback */


/*
 * This file has not been truncated
 */
