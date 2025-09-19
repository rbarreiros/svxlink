/**
@file	 AsyncConfigFactory.cpp
@brief   Factory class for creating configuration backends
@author  Assistant
@date	 2025-09-19

This file contains the implementation of the factory class for creating
configuration backends.

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
#include <sstream>
#include <algorithm>

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

#include "AsyncConfigFactory.h"
#include "AsyncFileConfigBackend.h"

// Include database backends only if they're available
#ifdef HAVE_SQLITE3
#include "AsyncSQLiteConfigBackend.h"
#endif

#ifdef HAVE_MYSQL
#include "AsyncMySQLConfigBackend.h"
#endif

#ifdef HAVE_POSTGRESQL
#include "AsyncPostgreSQLConfigBackend.h"
#endif

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

ConfigBackendPtr ConfigFactory::createBackend(const std::string& source)
{
  BackendType type = detectBackendType(source);
  
  switch (type)
  {
    case BACKEND_FILE:
    {
      std::unique_ptr<FileConfigBackend> backend(new FileConfigBackend());
      string file_path = source;
      
      // Remove file:// prefix if present
      if (file_path.find("file://") == 0)
      {
        file_path = file_path.substr(7);
      }
      
      if (backend->open(file_path))
      {
        return std::move(backend);
      }
      break;
    }
    
    case BACKEND_SQLITE:
    {
#ifdef HAVE_SQLITE3
      std::unique_ptr<SQLiteConfigBackend> backend(new SQLiteConfigBackend());
      string db_path = source.substr(9); // Remove "sqlite://" prefix
      
      if (backend->open(db_path))
      {
        return std::move(backend);
      }
#else
      cerr << "*** ERROR: SQLite support not compiled in" << endl;
#endif
      break;
    }
    
    case BACKEND_MYSQL:
    {
#ifdef HAVE_MYSQL
      std::unique_ptr<MySQLConfigBackend> backend(new MySQLConfigBackend());
      string connection_string;
      
      if (parseDatabaseURL(source, connection_string))
      {
        if (backend->open(connection_string))
        {
          return std::move(backend);
        }
      }
#else
      cerr << "*** ERROR: MySQL support not compiled in" << endl;
#endif
      break;
    }
    
    case BACKEND_POSTGRESQL:
    {
#ifdef HAVE_POSTGRESQL
      std::unique_ptr<PostgreSQLConfigBackend> backend(new PostgreSQLConfigBackend());
      string connection_string;
      
      if (parseDatabaseURL(source, connection_string))
      {
        if (backend->open(connection_string))
        {
          return std::move(backend);
        }
      }
#else
      cerr << "*** ERROR: PostgreSQL support not compiled in" << endl;
#endif
      break;
    }
    
    case BACKEND_UNKNOWN:
    default:
      cerr << "*** ERROR: Unknown configuration backend type for source: " << source << endl;
      break;
  }
  
  return nullptr;
} /* ConfigFactory::createBackend */

ConfigFactory::BackendType ConfigFactory::detectBackendType(const std::string& source)
{
  if (source.empty())
  {
    return BACKEND_UNKNOWN;
  }
  
  // Check for URL prefixes
  if (source.find("sqlite://") == 0)
  {
    return BACKEND_SQLITE;
  }
  else if (source.find("mysql://") == 0)
  {
    return BACKEND_MYSQL;
  }
  else if (source.find("postgresql://") == 0 || source.find("postgres://") == 0)
  {
    return BACKEND_POSTGRESQL;
  }
  else if (source.find("file://") == 0)
  {
    return BACKEND_FILE;
  }
  else
  {
    // Default to file backend for plain paths
    return BACKEND_FILE;
  }
} /* ConfigFactory::detectBackendType */

std::string ConfigFactory::getBackendTypeName(BackendType type)
{
  switch (type)
  {
    case BACKEND_FILE:
      return "file";
    case BACKEND_SQLITE:
      return "sqlite";
    case BACKEND_MYSQL:
      return "mysql";
    case BACKEND_POSTGRESQL:
      return "postgresql";
    case BACKEND_UNKNOWN:
    default:
      return "unknown";
  }
} /* ConfigFactory::getBackendTypeName */

bool ConfigFactory::isBackendAvailable(BackendType type)
{
  switch (type)
  {
    case BACKEND_FILE:
      return true; // Always available
    case BACKEND_SQLITE:
#ifdef HAVE_SQLITE3
      return true;
#else
      return false;
#endif
    case BACKEND_MYSQL:
#ifdef HAVE_MYSQL
      return true;
#else
      return false;
#endif
    case BACKEND_POSTGRESQL:
#ifdef HAVE_POSTGRESQL
      return true;
#else
      return false;
#endif
    case BACKEND_UNKNOWN:
    default:
      return false;
  }
} /* ConfigFactory::isBackendAvailable */

std::string ConfigFactory::getAvailableBackends(void)
{
  ostringstream backends;
  bool first = true;
  
  BackendType types[] = { BACKEND_FILE, BACKEND_SQLITE, BACKEND_MYSQL, BACKEND_POSTGRESQL };
  
  for (BackendType type : types)
  {
    if (isBackendAvailable(type))
    {
      if (!first)
      {
        backends << ", ";
      }
      first = false;
      backends << getBackendTypeName(type);
    }
  }
  
  return backends.str();
} /* ConfigFactory::getAvailableBackends */

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

bool ConfigFactory::parseDatabaseURL(const std::string& url, std::string& connection_string)
{
  // Parse URLs in the format: scheme://[user[:password]@]host[:port]/database[?options]
  
  size_t scheme_end = url.find("://");
  if (scheme_end == string::npos)
  {
    return false;
  }
  
  string scheme = url.substr(0, scheme_end);
  string remainder = url.substr(scheme_end + 3);
  
  // Extract user and password
  string user, password, host, database;
  unsigned int port = 0;
  
  size_t at_pos = remainder.find('@');
  string auth_part, host_part;
  
  if (at_pos != string::npos)
  {
    auth_part = remainder.substr(0, at_pos);
    host_part = remainder.substr(at_pos + 1);
    
    // Parse user:password
    size_t colon_pos = auth_part.find(':');
    if (colon_pos != string::npos)
    {
      user = auth_part.substr(0, colon_pos);
      password = auth_part.substr(colon_pos + 1);
    }
    else
    {
      user = auth_part;
    }
  }
  else
  {
    host_part = remainder;
  }
  
  // Parse host:port/database
  size_t slash_pos = host_part.find('/');
  if (slash_pos == string::npos)
  {
    return false; // Database name is required
  }
  
  string host_port = host_part.substr(0, slash_pos);
  database = host_part.substr(slash_pos + 1);
  
  // Remove query parameters from database name
  size_t question_pos = database.find('?');
  if (question_pos != string::npos)
  {
    database = database.substr(0, question_pos);
  }
  
  // Parse host:port
  size_t colon_pos = host_port.find(':');
  if (colon_pos != string::npos)
  {
    host = host_port.substr(0, colon_pos);
    port = static_cast<unsigned int>(stoul(host_port.substr(colon_pos + 1)));
  }
  else
  {
    host = host_port;
    // Use default ports
    if (scheme == "mysql")
    {
      port = 3306;
    }
    else if (scheme == "postgresql" || scheme == "postgres")
    {
      port = 5432;
    }
  }
  
  // Build connection string based on backend type
  ostringstream conn_str;
  
  if (scheme == "mysql")
  {
    conn_str << "host=" << host << ";port=" << port << ";user=" << user 
             << ";password=" << password << ";database=" << database;
  }
  else if (scheme == "postgresql" || scheme == "postgres")
  {
    conn_str << "host=" << host << " port=" << port << " user=" << user 
             << " password=" << password << " dbname=" << database;
  }
  else
  {
    return false;
  }
  
  connection_string = conn_str.str();
  return true;
} /* ConfigFactory::parseDatabaseURL */

/*
 * This file has not been truncated
 */
