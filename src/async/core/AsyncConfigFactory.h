/**
@file	 AsyncConfigFactory.h
@brief   Factory class for creating configuration backends
@author  Assistant
@date	 2025-09-19

This file contains the factory class for creating configuration backends
based on the source specification.

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

#ifndef ASYNC_CONFIG_FACTORY_INCLUDED
#define ASYNC_CONFIG_FACTORY_INCLUDED

/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <string>
#include <memory>

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
@brief	Factory class for creating configuration backends
@author Assistant
@date   2025-09-19

This class provides a factory method for creating configuration backends
based on the source specification. The factory automatically detects the
backend type and creates the appropriate implementation.

Source format examples:
- File backend: "/path/to/config.conf" or "file:///path/to/config.conf"
- SQLite backend: "sqlite:///path/to/database.db"
- MySQL backend: "mysql://user:password@host:port/database"
- PostgreSQL backend: "postgresql://user:password@host:port/database"
*/
class ConfigFactory
{
  public:
    /**
     * @brief   Backend type enumeration
     */
    enum BackendType
    {
      BACKEND_FILE,
      BACKEND_SQLITE,
      BACKEND_MYSQL,
      BACKEND_POSTGRESQL,
      BACKEND_UNKNOWN
    };

    /**
     * @brief 	Create a configuration backend
     * @param 	source The configuration source specification
     * @return	Returns a unique pointer to the created backend, or nullptr on failure
     *
     * This function creates a configuration backend based on the source specification.
     * The source format determines which backend type is created:
     * 
     * - File paths (with or without file:// prefix) create FileConfigBackend
     * - sqlite:// URLs create SQLiteConfigBackend
     * - mysql:// URLs create MySQLConfigBackend  
     * - postgresql:// URLs create PostgreSQLConfigBackend
     */
    static ConfigBackendPtr createBackend(const std::string& source);

    /**
     * @brief   Detect the backend type from the source specification
     * @param   source The configuration source specification
     * @return  Returns the detected backend type
     */
    static BackendType detectBackendType(const std::string& source);

    /**
     * @brief   Get the backend type name as a string
     * @param   type The backend type
     * @return  Returns the backend type name
     */
    static std::string getBackendTypeName(BackendType type);

    /**
     * @brief   Check if a backend type is available (compiled in)
     * @param   type The backend type to check
     * @return  Returns true if the backend is available
     */
    static bool isBackendAvailable(BackendType type);

    /**
     * @brief   Get a list of all available backend types
     * @return  Returns a string with comma-separated backend type names
     */
    static std::string getAvailableBackends(void);

  protected:

  private:
    /**
     * @brief   Private constructor (static class)
     */
    ConfigFactory(void) = delete;

    /**
     * @brief   Parse a database URL into connection parameters
     * @param   url The database URL
     * @param   connection_string The parsed connection string (output)
     * @return  Returns true on success
     */
    static bool parseDatabaseURL(const std::string& url, std::string& connection_string);

}; /* class ConfigFactory */

} /* namespace */

#endif /* ASYNC_CONFIG_FACTORY_INCLUDED */

/*
 * This file has not been truncated
 */
