/**
@file	 AsyncConfigManager.h
@brief   Configuration manager that handles backend selection and initialization
@author  Rui Barreiros
@date	 2025-09-19

This file contains the configuration manager that reads db.conf to determine
which backend to use and handles database initialization with default values.

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

#ifndef ASYNC_CONFIG_MANAGER_INCLUDED
#define ASYNC_CONFIG_MANAGER_INCLUDED

/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <string>
#include <map>

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

#include "AsyncConfigBackend.h"

/****************************************************************************
 *
 * Forward declarations
 *
 ****************************************************************************/

/****************************************************************************
 *
 * Namespace
 *
 ****************************************************************************/

namespace Async
{

/****************************************************************************
 *
 * Forward declarations of classes inside of the declared namespace
 *
 ****************************************************************************/

/****************************************************************************
 *
 * Defines & typedefs
 *
 ****************************************************************************/

/****************************************************************************
 *
 * Exported Global Variables
 *
 ****************************************************************************/

/****************************************************************************
 *
 * Class definitions
 *
 ****************************************************************************/

/**
@brief	Configuration manager for backend selection and initialization
@author Assistant
@date   2025-09-19

This class manages the configuration backend selection by reading a db.conf
file and initializing the appropriate backend. It also handles database
initialization with default SVXLink configuration values.

The db.conf file format:
[DATABASE]
TYPE=file|sqlite|mysql|postgresql
SOURCE=/path/to/file or connection string

Examples:
- TYPE=file, SOURCE=/etc/svxlink/svxlink.conf
- TYPE=sqlite, SOURCE=/var/lib/svxlink/config.db  
- TYPE=mysql, SOURCE=mysql://user:pass@localhost/svxlink
- TYPE=postgresql, SOURCE=postgresql://user:pass@localhost/svxlink
*/
class ConfigManager
{
  public:
    /**
     * @brief 	Default constructor
     */
    ConfigManager(void);
  
    /**
     * @brief 	Destructor
     */
    ~ConfigManager(void);
  
    /**
     * @brief 	Initialize configuration backend from db.conf
     * @param 	config_dir The directory to search for db.conf
     * @param 	default_config_file Default config filename to use if not specified in db.conf
     * @param 	default_table_prefix Default table prefix based on binary name (e.g., "svxlink_")
     * @return	Returns a configured backend or nullptr on failure
     *
     * This function searches for db.conf in the following locations:
     * 1. config_dir/db.conf (if config_dir is provided)
     * 2. ~/.svxlink/db.conf
     * 3. /etc/svxlink/db.conf
     * 4. SVX_SYSCONF_INSTALL_DIR/db.conf
     * 
     * If no db.conf is found, defaults to file backend with the specified
     * default_config_file in the same search locations.
     */
    ConfigBackendPtr initializeBackend(const std::string& config_dir = "", 
                                       const std::string& default_config_file = "svxlink.conf",
                                       const std::string& default_table_prefix = "");

    /**
     * @brief 	Initialize configuration backend from specific db.conf file
     * @param 	db_conf_path The full path to the db.conf file to use
     * @param 	default_config_file Default config filename to use if not specified in db.conf
     * @param 	default_table_prefix Default table prefix based on binary name (e.g., "svxlink_")
     * @return	Returns a configured backend or nullptr on failure
     *
     * This function directly uses the specified db.conf file instead of searching
     * for it in standard locations.
     */
    ConfigBackendPtr initializeBackendFromFile(const std::string& db_conf_path,
                                                const std::string& default_config_file = "svxlink.conf",
                                                const std::string& default_table_prefix = "");

    /**
     * @brief   Get the last error message
     * @return  Returns the last error message
     */
    const std::string& getLastError(void) const { return m_last_error; }

    /**
     * @brief Get the main configuration reference path for CFG_DIR resolution
     * @return Path to use as reference for relative CFG_DIR paths
     *
     * For file backend: returns the main config file path
     * For database backend: returns the db.conf file path
     */
    const std::string& getMainConfigReference(void) const { return m_main_config_reference; }

  protected:

  private:
    std::string m_last_error;
    std::string m_main_config_reference;

    struct DbConfig
    {
      std::string type;
      std::string source;
      std::string table_prefix;      // Optional: table prefix for DB isolation
      bool enable_change_notifications;
      unsigned int poll_interval_seconds;
      
      DbConfig() : enable_change_notifications(false), poll_interval_seconds(0) {}
    };

    bool findAndParseDbConfig(const std::string& config_dir, DbConfig& config, std::string& db_conf_path);
    bool parseDbConfigFile(const std::string& file_path, DbConfig& config);
    bool initializeDatabase(ConfigBackend* backend, const std::string& default_config_file);
    bool populateFromExistingFiles(ConfigBackend* backend, const std::string& config_file);
    void populateDefaultConfiguration(ConfigBackend* backend);
    std::string findConfigFile(const std::string& config_dir, const std::string& filename);

}; /* class ConfigManager */

} /* namespace */

#endif /* ASYNC_CONFIG_MANAGER_INCLUDED */

/*
 * This file has not been truncated
 */
