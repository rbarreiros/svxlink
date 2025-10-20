/**
@file	 AsyncConfigManager.cpp
@brief   Configuration manager implementation
@author  Assistant
@date	 2025-09-19

This file contains the implementation of the configuration manager.

\verbatim
Async - A library for programming event driven applications
Copyright (C) 2004-2025 Tobias Blomberg / SM0SVX

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
#include <fstream>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>
#include <vector>
#include <dirent.h>
#include <cstring>

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

#include "AsyncConfigManager.h"
#include "AsyncConfigBackend.h"
#include "AsyncConfigSource.h"

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

ConfigManager::ConfigManager(void)
{
} /* ConfigManager::ConfigManager */

ConfigManager::~ConfigManager(void)
{
} /* ConfigManager::~ConfigManager */

ConfigBackendPtr ConfigManager::initializeBackend(const std::string& config_dir)
{
  m_last_error.clear();
  
  DbConfig db_config;
  
  // Try to find and parse db.conf
  string db_conf_path;
  if (!findAndParseDbConfig(config_dir, db_config, db_conf_path))
  {
    // If no db.conf found, default to file backend with svxlink.conf
    cout << "No db.conf found, defaulting to file backend" << endl;
    
    string config_file = findConfigFile(config_dir, "svxlink.conf");
    if (config_file.empty())
    {
      m_last_error = "Neither db.conf nor svxlink.conf could be found in standard locations";
      cerr << "*** ERROR: " << m_last_error << endl;
      return nullptr;
    }
    
    db_config.type = "file";
    db_config.source = config_file;
    m_main_config_reference = config_file; // For file backend, use the config file itself
  }
  else
  {
    // For CFG_DIR resolution, use the actual config source file if it's a file backend
    if (db_config.type == "file")
    {
      m_main_config_reference = db_config.source; // Use the actual config file
    }
    else
    {
      m_main_config_reference = db_conf_path; // Use db.conf location for database backends
    }
  }
  
  // Check if the requested backend type is available
  if (!ConfigSource::isBackendAvailable(db_config.type))
  {
    m_last_error = "Backend '" + db_config.type + "' is not available (not compiled in)";
    cerr << "*** ERROR: " << m_last_error << endl;
    cerr << "Available backends: " << ConfigSource::availableBackendsString() << endl;
    return nullptr;
  }
  
  // Create the backend
  string source_url;
  if (db_config.type == "file")
  {
    source_url = db_config.source;
  }
  else if (db_config.type == "sqlite")
  {
    source_url = "sqlite://" + db_config.source;
  }
  else
  {
    source_url = db_config.source; // Already includes protocol for mysql/postgresql
  }
  
  ConfigBackendPtr backend = createConfigBackend(source_url);
  if (backend == nullptr)
  {
    m_last_error = "Failed to create " + db_config.type + " backend";
    cerr << "*** ERROR: " << m_last_error << endl;
    return nullptr;
  }
  
  // Apply db.conf notification settings to the backend
  backend->enableChangeNotifications(db_config.enable_change_notifications);
  if (db_config.enable_change_notifications && db_config.poll_interval_seconds > 0)
  {
    backend->startAutoPolling(db_config.poll_interval_seconds * 1000); // Convert to milliseconds
    cout << "Auto-polling enabled with interval: " << db_config.poll_interval_seconds << " seconds" << endl;
  }
  
  cout << "Successfully initialized " << backend->getBackendType() 
       << " configuration backend: " << backend->getBackendInfo() << endl;
  
  // For database backends, check if initialization is needed
  if (backend->getBackendType() != "file")
  {
    if (!initializeDatabase(backend.get()))
    {
      m_last_error = "Failed to initialize database backend";
      cerr << "*** ERROR: " << m_last_error << endl;
      return nullptr;
    }
  }
  
  return backend;
} /* ConfigManager::initializeBackend */

ConfigBackendPtr ConfigManager::initializeBackendFromFile(const std::string& db_conf_path)
{
  m_last_error.clear();
  
  DbConfig db_config;
  
  // Parse the specified db.conf file directly
  if (!parseDbConfigFile(db_conf_path, db_config))
  {
    m_last_error = "Could not parse database configuration file: " + db_conf_path;
    cerr << "*** ERROR: " << m_last_error << endl;
    return nullptr;
  }
  
  // For CFG_DIR resolution, use the actual config source file if it's a file backend
  if (db_config.type == "file")
  {
    m_main_config_reference = db_config.source; // Use the actual config file
  }
  else
  {
    m_main_config_reference = db_conf_path; // Use db.conf location for database backends
  }
  
  // Check if the requested backend type is available
  if (!ConfigSource::isBackendAvailable(db_config.type))
  {
    m_last_error = "Backend '" + db_config.type + "' is not available (not compiled in)";
    cerr << "*** ERROR: " << m_last_error << endl;
    cerr << "Available backends: " << ConfigSource::availableBackendsString() << endl;
    return nullptr;
  }
  
  // Create the backend
  string source_url;
  if (db_config.type == "file")
  {
    source_url = db_config.source;
  }
  else if (db_config.type == "sqlite")
  {
    source_url = "sqlite://" + db_config.source;
  }
  else
  {
    source_url = db_config.source; // Already includes protocol for mysql/postgresql
  }
  
  ConfigBackendPtr backend = createConfigBackend(source_url);
  if (backend == nullptr)
  {
    m_last_error = "Failed to create " + db_config.type + " backend";
    cerr << "*** ERROR: " << m_last_error << endl;
    return nullptr;
  }
  
  // Apply db.conf notification settings to the backend
  backend->enableChangeNotifications(db_config.enable_change_notifications);
  if (db_config.enable_change_notifications && db_config.poll_interval_seconds > 0)
  {
    backend->startAutoPolling(db_config.poll_interval_seconds * 1000); // Convert to milliseconds
    cout << "Auto-polling enabled with interval: " << db_config.poll_interval_seconds << " seconds" << endl;
  }
  
  cout << "Successfully initialized " << backend->getBackendType() 
       << " configuration backend: " << backend->getBackendInfo() << endl;
  
  // For database backends, check if initialization is needed
  if (backend->getBackendType() != "file")
  {
    if (!initializeDatabase(backend.get()))
    {
      m_last_error = "Failed to initialize database backend";
      cerr << "*** ERROR: " << m_last_error << endl;
      return nullptr;
    }
  }
  
  return backend;
} /* ConfigManager::initializeBackendFromFile */

/****************************************************************************
 *
 * Protected member functions
 *
 ****************************************************************************/

/****************************************************************************
 *
 * Private member functions
 *
 ****************************************************************************/

bool ConfigManager::findAndParseDbConfig(const std::string& config_dir, DbConfig& config, std::string& db_conf_path)
{
  db_conf_path = findConfigFile(config_dir, "db.conf");
  if (db_conf_path.empty())
  {
    return false;
  }
  
  return parseDbConfigFile(db_conf_path, config);
} /* ConfigManager::findAndParseDbConfig */

bool ConfigManager::parseDbConfigFile(const std::string& file_path, DbConfig& config)
{
  ifstream file(file_path);
  if (!file.is_open())
  {
    return false;
  }
  
  cout << "Reading database configuration from: " << file_path << endl;
  
  string line;
  string current_section;
  bool in_database_section = false;
  
  while (getline(file, line))
  {
    // Remove leading/trailing whitespace
    size_t start = line.find_first_not_of(" \t\r\n");
    if (start == string::npos)
    {
      continue; // Empty line
    }
    
    size_t end = line.find_last_not_of(" \t\r\n");
    line = line.substr(start, end - start + 1);
    
    // Skip comments
    if (line[0] == '#' || line[0] == ';')
    {
      continue;
    }
    
    // Check for section header
    if (line[0] == '[' && line[line.length() - 1] == ']')
    {
      current_section = line.substr(1, line.length() - 2);
      in_database_section = (current_section == "DATABASE");
      continue;
    }
    
    // Parse key=value pairs in DATABASE section
    if (in_database_section)
    {
      size_t eq_pos = line.find('=');
      if (eq_pos != string::npos)
      {
        string key = line.substr(0, eq_pos);
        string value = line.substr(eq_pos + 1);
        
        // Remove whitespace around key and value
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        
        if (key == "TYPE")
        {
          config.type = value;
        }
        else if (key == "SOURCE")
        {
          config.source = value;
        }
        else if (key == "ENABLE_CHANGE_NOTIFICATIONS")
        {
          config.enable_change_notifications = (value == "1" || value == "true" || value == "TRUE" || value == "yes" || value == "YES");
        }
        else if (key == "POLL_INTERVAL")
        {
          config.poll_interval_seconds = std::stoul(value);
        }
      }
    }
  }
  
  file.close();
  
  if (config.type.empty() || config.source.empty())
  {
    cerr << "*** ERROR: Invalid db.conf - missing TYPE or SOURCE in [DATABASE] section" << endl;
    return false;
  }
  
  cout << "Database configuration: TYPE=" << config.type << ", SOURCE=" << config.source << endl;
  return true;
} /* ConfigManager::parseDbConfigFile */

bool ConfigManager::initializeDatabase(ConfigBackend* backend)
{
  if (backend == nullptr)
  {
    return false;
  }
  
  // Check if database is already initialized by trying to list sections
  list<string> sections = backend->listSections();
  if (!sections.empty())
  {
    cout << "Database already initialized with " << sections.size() << " sections" << endl;
    return true;
  }
  
  cout << "Database is empty, initializing..." << endl;
  
  // Disable change notifications during initialization to avoid spurious signals
  bool was_enabled = backend->changeNotificationsEnabled();
  bool was_polling = backend->isAutoPolling();
  unsigned int poll_interval_ms = backend->getPollingInterval();
  if (was_enabled)
  {
    //cout << "[DEBUG] Disabling change notifications during database initialization" << endl;
    backend->enableChangeNotifications(false);
  }
  if (was_polling)
  {
    //cout << "[DEBUG] Stopping auto-polling during database initialization (was " << poll_interval_ms << "ms)" << endl;
    backend->stopAutoPolling();
  }
  
  // Try to populate from existing file configuration first
  if (populateFromExistingFiles(backend))
  {
    cout << "Database initialized from existing configuration files" << endl;
  }
  else
  {
    cout << "No existing configuration files found, using default configuration..." << endl;
    populateDefaultConfiguration(backend);
  }
  
  // Re-enable change notifications and polling if they were enabled before
  if (was_enabled)
  {
    //cout << "[DEBUG] Re-enabling change notifications after database initialization" << endl;
    backend->enableChangeNotifications(true);
  }
  if (was_polling && poll_interval_ms > 0)
  {
    //cout << "[DEBUG] Restarting auto-polling after database initialization (" << poll_interval_ms << "ms)" << endl;
    backend->startAutoPolling(poll_interval_ms);
  }
  
  // Verify initialization
  sections = backend->listSections();
  if (sections.empty())
  {
    cerr << "*** ERROR: Failed to initialize database" << endl;
    return false;
  }
  
  cout << "Database initialized successfully with " << sections.size() << " sections" << endl;
  return true;
} /* ConfigManager::initializeDatabase */

bool ConfigManager::populateFromExistingFiles(ConfigBackend* backend)
{
  if (backend == nullptr)
  {
    return false;
  }
  
  // Look for existing svxlink.conf in standard locations
  string config_file = findConfigFile("", "svxlink.conf");
  if (config_file.empty())
  {
    return false; // No existing configuration files found
  }
  
  cout << "Found existing configuration file: " << config_file << endl;
  cout << "Loading existing configuration to populate database..." << endl;
  
  // Create a temporary file backend to read the existing configuration
  ConfigBackendPtr file_backend = createConfigBackend("file://" + config_file);
  if (file_backend == nullptr)
  {
    cerr << "*** WARNING: Could not create file backend for " << config_file << endl;
    return false;
  }
  
  // Copy all sections and values from file backend to database backend
  list<string> sections = file_backend->listSections();
  for (const string& section : sections)
  {
    list<string> tags = file_backend->listSection(section);
    for (const string& tag : tags)
    {
      string value;
      if (file_backend->getValue(section, tag, value))
      {
        if (!backend->setValue(section, tag, value))
        {
          cerr << "*** WARNING: Failed to set " << section << "/" << tag << " in database" << endl;
        }
      }
    }
  }
  
  // Process CFG_DIR if specified in the main config file
  string cfg_dir;
  if (file_backend->getValue("GLOBAL", "CFG_DIR", cfg_dir))
  {
    // Make CFG_DIR path absolute if it's relative
    if (cfg_dir[0] != '/')
    {
      int slash_pos = config_file.rfind('/');
      if (slash_pos != -1)
      {
        cfg_dir = config_file.substr(0, slash_pos+1) + cfg_dir;
      }
      else
      {
        cfg_dir = string("./") + cfg_dir;
      }
    }
    
    cout << "Processing CFG_DIR: " << cfg_dir << endl;
    
    DIR *dir = opendir(cfg_dir.c_str());
    if (dir != NULL)
    {
      struct dirent *dirent;
      while ((dirent = readdir(dir)) != NULL)
      {
        char *dot = strrchr(dirent->d_name, '.');
        if ((dot == NULL) || (dirent->d_name[0] == '.') ||
            (strcmp(dot, ".conf") != 0))
        {
          continue;
        }
        
        string cfg_file_path = cfg_dir + "/" + dirent->d_name;
        cout << "Loading additional config file: " << cfg_file_path << endl;
        
        ConfigBackendPtr additional_backend = createConfigBackend("file://" + cfg_file_path);
        if (additional_backend != nullptr)
        {
          // Copy all sections and values from this additional file
          list<string> add_sections = additional_backend->listSections();
          for (const string& section : add_sections)
          {
            list<string> add_tags = additional_backend->listSection(section);
            for (const string& tag : add_tags)
            {
              string value;
              if (additional_backend->getValue(section, tag, value))
              {
                if (!backend->setValue(section, tag, value))
                {
                  cerr << "*** WARNING: Failed to set " << section << "/" << tag << " from " << cfg_file_path << endl;
                }
              }
            }
          }
        }
        else
        {
          cerr << "*** WARNING: Could not load additional config file: " << cfg_file_path << endl;
        }
      }
      closedir(dir);
    }
    else
    {
      cerr << "*** WARNING: Could not open CFG_DIR: " << cfg_dir << endl;
    }
  }
  
  return true;
} /* ConfigManager::populateFromExistingFiles */

void ConfigManager::populateDefaultConfiguration(ConfigBackend* backend)
{
  if (backend == nullptr)
  {
    return;
  }
  
  // GLOBAL section - core configuration
  backend->setValue("GLOBAL", "LOGICS", "SimplexLogic");
  backend->setValue("GLOBAL", "CFG_DIR", "/etc/svxlink");
  backend->setValue("GLOBAL", "TIMESTAMP_FORMAT", "%c");
  backend->setValue("GLOBAL", "CARD_SAMPLE_RATE", "48000");
  backend->setValue("GLOBAL", "LOCATION_INFO", "LocationInfo");
  
  // SimplexLogic section - basic simplex logic
  backend->setValue("SimplexLogic", "TYPE", "Simplex");
  backend->setValue("SimplexLogic", "RX", "Rx1");
  backend->setValue("SimplexLogic", "TX", "Tx1");
  backend->setValue("SimplexLogic", "MODULES", "ModuleHelp,ModuleParrot");
  backend->setValue("SimplexLogic", "CALLSIGN", "NOCALL");
  backend->setValue("SimplexLogic", "SHORT_IDENT_INTERVAL", "5");
  backend->setValue("SimplexLogic", "LONG_IDENT_INTERVAL", "60");
  backend->setValue("SimplexLogic", "IDENT_ONLY_AFTER_TX", "1");
  backend->setValue("SimplexLogic", "EXEC_CMD_ON_SQL_CLOSE", "1");
  
  // Rx1 section - receiver configuration
  backend->setValue("Rx1", "TYPE", "Local");
  backend->setValue("Rx1", "AUDIO_DEV", "alsa:plughw:0");
  backend->setValue("Rx1", "AUDIO_CHANNEL", "0");
  backend->setValue("Rx1", "SQL_DET", "VOX");
  backend->setValue("Rx1", "VOX_FILTER_DEPTH", "20");
  backend->setValue("Rx1", "VOX_LIMIT", "-18");
  backend->setValue("Rx1", "PREAMP", "0");
  backend->setValue("Rx1", "PEAK_METER", "1");
  backend->setValue("Rx1", "DTMF_DEC_TYPE", "INTERNAL");
  backend->setValue("Rx1", "DTMF_MUTING", "1");
  backend->setValue("Rx1", "DTMF_HANGTIME", "40");
  
  // Tx1 section - transmitter configuration  
  backend->setValue("Tx1", "TYPE", "Local");
  backend->setValue("Tx1", "AUDIO_DEV", "alsa:plughw:0");
  backend->setValue("Tx1", "AUDIO_CHANNEL", "0");
  backend->setValue("Tx1", "PTT_TYPE", "NONE");
  backend->setValue("Tx1", "TIMEOUT", "300");
  backend->setValue("Tx1", "TX_DELAY", "0");
  backend->setValue("Tx1", "PREEMPHASIS", "1");
  backend->setValue("Tx1", "DTMF_TONE_LENGTH", "100");
  backend->setValue("Tx1", "DTMF_TONE_SPACING", "50");
  backend->setValue("Tx1", "DTMF_DIGIT_PWR", "-15");
  
  // LocationInfo section - location information
  backend->setValue("LocationInfo", "CALLSIGN", "NOCALL");
  backend->setValue("LocationInfo", "NAME", "SVXLink Node");
  backend->setValue("LocationInfo", "DESCRIPTION", "SVXLink simplex node");
  backend->setValue("LocationInfo", "SPONSOR", "");
  backend->setValue("LocationInfo", "FREQUENCY", "0.0");
  backend->setValue("LocationInfo", "URL", "");
  backend->setValue("LocationInfo", "QTH", "");
  
  cout << "Default configuration populated successfully" << endl;
} /* ConfigManager::populateDefaultConfiguration */

std::string ConfigManager::findConfigFile(const std::string& config_dir, const std::string& filename)
{
  vector<string> search_paths;
  
  // Add user-specified config directory first
  if (!config_dir.empty())
  {
    search_paths.push_back(config_dir + "/" + filename);
  }
  
  // Add standard search paths
  const char* home = getenv("HOME");
  if (home != nullptr)
  {
    search_paths.push_back(string(home) + "/.svxlink/" + filename);
  }
  
  search_paths.push_back("/etc/svxlink/" + filename);
  search_paths.push_back(string(SVX_SYSCONF_INSTALL_DIR) + "/" + filename);
  
  // Check each path
  for (const string& path : search_paths)
  {
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode))
    {
      // Check if file is readable
      if (access(path.c_str(), R_OK) == 0)
      {
        return path;
      }
    }
  }
  
  return ""; // Not found
} /* ConfigManager::findConfigFile */

/*
 * This file has not been truncated
 */
