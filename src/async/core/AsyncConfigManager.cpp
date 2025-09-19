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
#include "AsyncConfigFactory.h"

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
  if (!findAndParseDbConfig(config_dir, db_config))
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
  }
  
  // Check if the requested backend type is available
  ConfigFactory::BackendType backend_type = ConfigFactory::BACKEND_UNKNOWN;
  if (db_config.type == "file")
  {
    backend_type = ConfigFactory::BACKEND_FILE;
  }
  else if (db_config.type == "sqlite")
  {
    backend_type = ConfigFactory::BACKEND_SQLITE;
  }
  else if (db_config.type == "mysql")
  {
    backend_type = ConfigFactory::BACKEND_MYSQL;
  }
  else if (db_config.type == "postgresql")
  {
    backend_type = ConfigFactory::BACKEND_POSTGRESQL;
  }
  else
  {
    m_last_error = "Unknown backend type: " + db_config.type;
    cerr << "*** ERROR: " << m_last_error << endl;
    return nullptr;
  }
  
  if (!ConfigFactory::isBackendAvailable(backend_type))
  {
    m_last_error = "Backend '" + db_config.type + "' is not available (not compiled in)";
    cerr << "*** ERROR: " << m_last_error << endl;
    cerr << "Available backends: " << ConfigFactory::getAvailableBackends() << endl;
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
  
  ConfigBackendPtr backend = ConfigFactory::createBackend(source_url);
  if (backend == nullptr)
  {
    m_last_error = "Failed to create " + db_config.type + " backend";
    cerr << "*** ERROR: " << m_last_error << endl;
    return nullptr;
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

bool ConfigManager::findAndParseDbConfig(const std::string& config_dir, DbConfig& config)
{
  string db_conf_path = findConfigFile(config_dir, "db.conf");
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
  
  cout << "Database is empty, initializing with default configuration..." << endl;
  
  // Populate with default configuration
  populateDefaultConfiguration(backend);
  
  // Verify initialization
  sections = backend->listSections();
  if (sections.empty())
  {
    cerr << "*** ERROR: Failed to initialize database with default configuration" << endl;
    return false;
  }
  
  cout << "Database initialized successfully with " << sections.size() << " sections" << endl;
  return true;
} /* ConfigManager::initializeDatabase */

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
