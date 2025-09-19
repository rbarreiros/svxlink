/**
@file	 AsyncConfigBackend.h
@brief   Abstract base class for configuration backends
@author  Assistant
@date	 2025-09-19

This file contains the abstract base class for configuration backends that
can load configuration data from various sources like files, databases, etc.

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

#ifndef ASYNC_CONFIG_BACKEND_INCLUDED
#define ASYNC_CONFIG_BACKEND_INCLUDED

/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <string>
#include <map>
#include <list>
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
@brief	Abstract base class for configuration backends
@author Assistant
@date   2025-09-19

This is an abstract base class that defines the interface for configuration
backends. Different implementations can load configuration data from various
sources such as INI files, MySQL, PostgreSQL, SQLite databases, etc.
*/
class ConfigBackend
{
  public:
    /**
     * @brief 	Default constructor
     */
    ConfigBackend(void) {}
  
    /**
     * @brief 	Destructor
     */
    virtual ~ConfigBackend(void) {}
  
    /**
     * @brief 	Open/connect to the configuration source
     * @param 	source The configuration source (file path, connection string, etc.)
     * @return	Returns \em true on success or else \em false.
     *
     * This function will establish a connection to the configuration source.
     * For file backends, this opens the file. For database backends, this
     * establishes a database connection.
     */
    virtual bool open(const std::string& source) = 0;
    
    /**
     * @brief 	Close/disconnect from the configuration source
     *
     * This function closes the connection to the configuration source and
     * performs any necessary cleanup.
     */
    virtual void close(void) = 0;
    
    /**
     * @brief 	Check if the backend is connected/open
     * @return	Returns \em true if connected, \em false otherwise
     */
    virtual bool isOpen(void) const = 0;
    
    /**
     * @brief 	Get the string value of a configuration variable
     * @param 	section The name of the section where the configuration
     *	      	      	variable is located
     * @param 	tag   	The name of the configuration variable to get
     * @param 	value 	The value is returned in this argument
     * @return	Returns \em true on success or else \em false on failure
     *
     * This function retrieves the value for a configuration variable.
     */
    virtual bool getValue(const std::string& section, const std::string& tag,
                          std::string& value) const = 0;

    /**
     * @brief 	Set the value of a configuration variable
     * @param 	section   The name of the section where the configuration
     *	      	      	  variable is located
     * @param 	tag   	  The name of the configuration variable to set.
     * @param   value     The value to set
     * @return	Returns \em true on success or else \em false on failure
     *
     * This function sets the value of a configuration variable.
     * Not all backends may support writing (e.g., read-only file backends).
     */
    virtual bool setValue(const std::string& section, const std::string& tag,
                          const std::string& value) = 0;

    /**
     * @brief   Return the name of all configuration sections
     * @return  Returns a list of all existing section names
     */
    virtual std::list<std::string> listSections(void) const = 0;

    /**
     * @brief 	Return the name of all the tags in the given section
     * @param 	section The name of the section where the configuration
     *	      	      	variables are located
     * @return	Returns the list of tags in the given section
     */
    virtual std::list<std::string> listSection(const std::string& section) const = 0;

    /**
     * @brief   Get backend type identifier
     * @return  Returns a string identifying the backend type
     */
    virtual std::string getBackendType(void) const = 0;

    /**
     * @brief   Get backend-specific information
     * @return  Returns a string with backend-specific details
     */
    virtual std::string getBackendInfo(void) const = 0;

  protected:

  private:
    // Disable copy constructor and assignment operator
    ConfigBackend(const ConfigBackend&) = delete;
    ConfigBackend& operator=(const ConfigBackend&) = delete;

}; /* class ConfigBackend */

/**
 * @brief Smart pointer type for ConfigBackend
 */
using ConfigBackendPtr = std::unique_ptr<ConfigBackend>;

} /* namespace */

#endif /* ASYNC_CONFIG_BACKEND_INCLUDED */

/*
 * This file has not been truncated
 */
