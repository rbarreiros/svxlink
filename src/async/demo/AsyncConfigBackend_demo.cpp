/**
@file	 AsyncConfigBackend_demo.cpp
@brief   Demo program showing the new configuration backend system
@author  Assistant
@date	 2025-09-19

This program demonstrates how to use the new configuration backend abstraction
layer to load configuration from different sources.

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
#include <string>
#include <list>
#include <fstream>

/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncConfig.h>
#include <AsyncConfigFactory.h>

/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

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

void demonstrateBackend(const string& config_dir);
void showAvailableBackends();
void createSampleDbConfig(const string& config_dir);
void demonstrateDbConfigInit(void);

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
 * Main program
 *
 ****************************************************************************/

int main(int argc, char **argv)
{
  cout << "SVXLink Configuration Backend Demo" << endl;
  cout << "==================================" << endl << endl;

  // Show available backends
  showAvailableBackends();

  // Demonstrate the new db.conf initialization method
  cout << "=== New db.conf Initialization Method ===" << endl;
  demonstrateDbConfigInit();

  if (argc >= 2)
  {
    string config_dir = argv[1];
    cout << "\n=== Custom Configuration Directory ===" << endl;
    cout << "Using configuration directory: " << config_dir << endl;
    
    // Create sample db.conf
    createSampleDbConfig(config_dir);
    
    // Demonstrate the backend
    demonstrateBackend(config_dir);
  }
  else
  {
    cout << "\nUsage: " << argv[0] << " [config_directory]" << endl;
    cout << endl;
    cout << "If no config directory is specified, the standard search paths will be used:" << endl;
    cout << "  ~/.svxlink/db.conf" << endl;
    cout << "  /etc/svxlink/db.conf" << endl;
    cout << "  /usr/local/etc/svxlink/db.conf (or system install directory)" << endl;
  }

  return 0;
} /* main */

/****************************************************************************
 *
 * Functions
 *
 ****************************************************************************/

void showAvailableBackends()
{
  cout << "Available configuration backends: " 
       << ConfigFactory::getAvailableBackends() << endl << endl;

  // Show detailed availability
  cout << "Backend availability:" << endl;
  cout << "  File:       " 
       << (ConfigFactory::isBackendAvailable(ConfigFactory::BACKEND_FILE) ? "Yes" : "No") 
       << endl;
  cout << "  SQLite:     " 
       << (ConfigFactory::isBackendAvailable(ConfigFactory::BACKEND_SQLITE) ? "Yes" : "No") 
       << endl;
  cout << "  MySQL:      " 
       << (ConfigFactory::isBackendAvailable(ConfigFactory::BACKEND_MYSQL) ? "Yes" : "No") 
       << endl;
  cout << "  PostgreSQL: " 
       << (ConfigFactory::isBackendAvailable(ConfigFactory::BACKEND_POSTGRESQL) ? "Yes" : "No") 
       << endl << endl;
} /* showAvailableBackends */

void demonstrateDbConfigInit(void)
{
  cout << "Demonstrating automatic backend initialization using db.conf..." << endl;
  
  // This will fail gracefully since no db.conf exists in standard locations
  // and no svxlink.conf exists either, but it shows the process
  cout << "Attempting to initialize configuration (this may fail in demo environment):" << endl;
  
  try 
  {
    Config cfg;
    // This would normally read db.conf and initialize the backend
    // In a real application, this would either succeed or call exit(1)
    cout << "Note: In a real application, cfg.open() would either succeed or abort the application" << endl;
  }
  catch (...)
  {
    cout << "Demo: Configuration initialization would handle errors and abort if needed" << endl;
  }
  
  cout << endl;
} /* demonstrateDbConfigInit */

void createSampleDbConfig(const string& config_dir)
{
  cout << "Creating sample db.conf in: " << config_dir << endl;
  
  // Create directory if it doesn't exist
  string mkdir_cmd = "mkdir -p " + config_dir;
  int result = system(mkdir_cmd.c_str());
  if (result != 0)
  {
    cerr << "Warning: Failed to create directory: " << config_dir << endl;
  }
  
  // Create a simple SQLite db.conf
  string db_conf_path = config_dir + "/db.conf";
  ofstream db_conf(db_conf_path);
  if (db_conf.is_open())
  {
    db_conf << "# SVXLink Database Configuration - Demo\n";
    db_conf << "[DATABASE]\n";
    db_conf << "TYPE=sqlite\n";
    db_conf << "SOURCE=" << config_dir << "/demo_config.db\n";
    db_conf.close();
    cout << "Created " << db_conf_path << endl;
  }
  
  cout << endl;
} /* createSampleDbConfig */

void demonstrateBackend(const string& config_dir)
{
  cout << "Demonstrating configuration backend with config directory: " << config_dir << endl;
  
  // Open configuration using db.conf
  Config cfg;
  if (!cfg.open(config_dir))
  {
    cout << "ERROR: Failed to open configuration" << endl;
    return;
  }
  
  cout << "Configuration opened successfully." << endl << endl;
  
  // List all sections
  cout << "Configuration sections:" << endl;
  list<string> sections = cfg.listSections();
  for (const string& section : sections)
  {
    cout << "  [" << section << "]" << endl;
    
    // List tags in this section
    list<string> tags = cfg.listSection(section);
    for (const string& tag : tags)
    {
      string value = cfg.getValue(section, tag);
      cout << "    " << tag << " = " << value << endl;
    }
    cout << endl;
  }
  
  // Demonstrate template getValue functions
  cout << "Template getValue demonstrations:" << endl;
  
  // String value
  string logics;
  if (cfg.getValue("GLOBAL", "LOGICS", logics))
  {
    cout << "  GLOBAL/LOGICS = \"" << logics << "\"" << endl;
  }
  
  // Integer value
  int vox_depth = 0;
  if (cfg.getValue("Rx1", "VOX_FILTER_DEPTH", vox_depth))
  {
    cout << "  Rx1/VOX_FILTER_DEPTH = " << vox_depth << endl;
  }
  
  // Integer with range checking
  int vox_limit = 0;
  if (cfg.getValue("Rx1", "VOX_LIMIT", -30, 0, vox_limit))
  {
    cout << "  Rx1/VOX_LIMIT = " << vox_limit << " (range checked)" << endl;
  }
  
  // Missing value with default
  string missing_value;
  if (cfg.getValue("GLOBAL", "MISSING_VALUE", missing_value, true))
  {
    cout << "  GLOBAL/MISSING_VALUE = \"" << missing_value << "\" (missing_ok=true)" << endl;
  }
  else
  {
    cout << "  GLOBAL/MISSING_VALUE not found (as expected)" << endl;
  }
  
  cout << endl;
  
  // Demonstrate value subscription
  cout << "Demonstrating value subscription..." << endl;
  
  cfg.subscribeValue("GLOBAL", "LOGICS", string("DefaultLogic"), 
                     [](const string& value) {
                       cout << "  Subscription callback: GLOBAL/LOGICS changed to \"" 
                            << value << "\"" << endl;
                     });
  
  // Change the value to trigger subscription
  cfg.setValue("GLOBAL", "LOGICS", "NewLogic");
  
  cout << endl << "Demo completed successfully!" << endl;
} /* demonstrateBackend */

/*
 * This file has not been truncated
 */
