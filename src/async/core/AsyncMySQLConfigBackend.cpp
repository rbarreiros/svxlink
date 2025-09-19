/**
@file	 AsyncMySQLConfigBackend.cpp
@brief   MySQL/MariaDB-based configuration backend implementation
@author  Assistant
@date	 2025-09-19

This file contains the implementation of the MySQL/MariaDB-based configuration backend.

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

#include <mysql/mysql.h>
#include <iostream>
#include <sstream>

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

#include "AsyncMySQLConfigBackend.h"

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

MySQLConfigBackend::MySQLConfigBackend(void)
  : m_mysql(nullptr)
{
  m_conn_params.port = 3306; // Default MySQL port
} /* MySQLConfigBackend::MySQLConfigBackend */

MySQLConfigBackend::~MySQLConfigBackend(void)
{
  close();
} /* MySQLConfigBackend::~MySQLConfigBackend */

bool MySQLConfigBackend::open(const string& source)
{
  close();
  
  m_connection_string = source;
  
  if (!parseConnectionString(source, m_conn_params))
  {
    cerr << "*** ERROR: Invalid MySQL connection string format" << endl;
    return false;
  }
  
  m_mysql = mysql_init(nullptr);
  if (m_mysql == nullptr)
  {
    cerr << "*** ERROR: Failed to initialize MySQL connection" << endl;
    return false;
  }
  
  // Set connection timeout
  unsigned int timeout = 10;
  mysql_options(m_mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
  
  // Enable automatic reconnection
  bool reconnect = true;
  mysql_options(m_mysql, MYSQL_OPT_RECONNECT, &reconnect);
  
  if (mysql_real_connect(m_mysql,
                         m_conn_params.host.c_str(),
                         m_conn_params.user.c_str(),
                         m_conn_params.password.c_str(),
                         m_conn_params.database.c_str(),
                         m_conn_params.port,
                         nullptr, 0) == nullptr)
  {
    cerr << "*** ERROR: Failed to connect to MySQL database: " << getLastError() << endl;
    close();
    return false;
  }
    
  // Create tables if they don't exist
  if (!createTables())
  {
    cerr << "*** ERROR: Failed to create database tables" << endl;
    close();
    return false;
  }
  
  return true;
} /* MySQLConfigBackend::open */

void MySQLConfigBackend::close(void)
{
  if (m_mysql != nullptr)
  {
    mysql_close(m_mysql);
    m_mysql = nullptr;
  }
  m_connection_string.clear();
} /* MySQLConfigBackend::close */

bool MySQLConfigBackend::isOpen(void) const
{
  return (m_mysql != nullptr);
} /* MySQLConfigBackend::isOpen */

bool MySQLConfigBackend::getValue(const std::string& section, const std::string& tag,
                                  std::string& value) const
{
  if (!isOpen())
  {
    return false;
  }
  
  string escaped_section = escapeString(section);
  string escaped_tag = escapeString(tag);
  
  ostringstream query;
  query << "SELECT value FROM config WHERE section = '" << escaped_section 
        << "' AND tag = '" << escaped_tag << "'";
  
  if (mysql_query(m_mysql, query.str().c_str()) != 0)
  {
    cerr << "*** ERROR: Failed to execute SELECT query: " << getLastError() << endl;
    return false;
  }
  
  MYSQL_RES* result = mysql_store_result(m_mysql);
  if (result == nullptr)
  {
    cerr << "*** ERROR: Failed to get query result: " << getLastError() << endl;
    return false;
  }
  
  MYSQL_ROW row = mysql_fetch_row(result);
  if (row != nullptr && row[0] != nullptr)
  {
    value = row[0];
    mysql_free_result(result);
    return true;
  }
  
  mysql_free_result(result);
  return false;
} /* MySQLConfigBackend::getValue */

bool MySQLConfigBackend::setValue(const std::string& section, const std::string& tag,
                                  const std::string& value)
{
  if (!isOpen())
  {
    return false;
  }
  
  string escaped_section = escapeString(section);
  string escaped_tag = escapeString(tag);
  string escaped_value = escapeString(value);
  
  ostringstream query;
  query << "INSERT INTO config (section, tag, value) VALUES ('"
        << escaped_section << "', '" << escaped_tag << "', '" << escaped_value
        << "') ON DUPLICATE KEY UPDATE value = '" << escaped_value 
        << "', updated_at = CURRENT_TIMESTAMP";
  
  if (mysql_query(m_mysql, query.str().c_str()) != 0)
  {
    cerr << "*** ERROR: Failed to execute INSERT/UPDATE query: " << getLastError() << endl;
    return false;
  }
  
  return true;
} /* MySQLConfigBackend::setValue */

list<string> MySQLConfigBackend::listSections(void) const
{
  list<string> sections;
  
  if (!isOpen())
  {
    return sections;
  }
  
  const char* query = "SELECT DISTINCT section FROM config ORDER BY section";
  
  if (mysql_query(m_mysql, query) != 0)
  {
    cerr << "*** ERROR: Failed to execute SELECT DISTINCT query: " << getLastError() << endl;
    return sections;
  }
  
  MYSQL_RES* result = mysql_store_result(m_mysql);
  if (result == nullptr)
  {
    cerr << "*** ERROR: Failed to get query result: " << getLastError() << endl;
    return sections;
  }
  
  MYSQL_ROW row;
  while ((row = mysql_fetch_row(result)) != nullptr)
  {
    if (row[0] != nullptr)
    {
      sections.push_back(row[0]);
    }
  }
  
  mysql_free_result(result);
  return sections;
} /* MySQLConfigBackend::listSections */

list<string> MySQLConfigBackend::listSection(const string& section) const
{
  list<string> tags;
  
  if (!isOpen())
  {
    return tags;
  }
  
  string escaped_section = escapeString(section);
  
  ostringstream query;
  query << "SELECT tag FROM config WHERE section = '" << escaped_section 
        << "' ORDER BY tag";
  
  if (mysql_query(m_mysql, query.str().c_str()) != 0)
  {
    cerr << "*** ERROR: Failed to execute SELECT tags query: " << getLastError() << endl;
    return tags;
  }
  
  MYSQL_RES* result = mysql_store_result(m_mysql);
  if (result == nullptr)
  {
    cerr << "*** ERROR: Failed to get query result: " << getLastError() << endl;
    return tags;
  }
  
  MYSQL_ROW row;
  while ((row = mysql_fetch_row(result)) != nullptr)
  {
    if (row[0] != nullptr)
    {
      tags.push_back(row[0]);
    }
  }
  
  mysql_free_result(result);
  return tags;
} /* MySQLConfigBackend::listSection */

std::string MySQLConfigBackend::getBackendType(void) const
{
  return "mysql";
} /* MySQLConfigBackend::getBackendType */

std::string MySQLConfigBackend::getBackendInfo(void) const
{
  if (!isOpen())
  {
    return "Not connected";
  }
  
  ostringstream info;
  info << "host=" << m_conn_params.host 
       << ";port=" << m_conn_params.port
       << ";user=" << m_conn_params.user
       << ";database=" << m_conn_params.database;
  
  return info.str();
} /* MySQLConfigBackend::getBackendInfo */

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

bool MySQLConfigBackend::parseConnectionString(const std::string& conn_str, ConnectionParams& params)
{
  // Parse connection string format: "host=hostname;port=3306;user=username;password=password;database=dbname"
  istringstream iss(conn_str);
  string token;
  
  while (getline(iss, token, ';'))
  {
    size_t eq_pos = token.find('=');
    if (eq_pos == string::npos)
    {
      continue;
    }
    
    string key = token.substr(0, eq_pos);
    string value = token.substr(eq_pos + 1);
    
    if (key == "host")
    {
      params.host = value;
    }
    else if (key == "port")
    {
      params.port = static_cast<unsigned int>(stoul(value));
    }
    else if (key == "user")
    {
      params.user = value;
    }
    else if (key == "password")
    {
      params.password = value;
    }
    else if (key == "database")
    {
      params.database = value;
    }
  }
  
  // Validate required parameters
  if (params.host.empty() || params.user.empty() || params.database.empty())
  {
    return false;
  }
  
  return true;
} /* MySQLConfigBackend::parseConnectionString */

bool MySQLConfigBackend::createTables(void)
{
  const char* create_table_sql = 
    "CREATE TABLE IF NOT EXISTS config ("
    "  id INT AUTO_INCREMENT PRIMARY KEY,"
    "  section VARCHAR(255) NOT NULL,"
    "  tag VARCHAR(255) NOT NULL,"
    "  value TEXT NOT NULL,"
    "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
    "  updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
    "  UNIQUE KEY unique_config (section, tag),"
    "  INDEX idx_section (section)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8";
  
  if (mysql_query(m_mysql, create_table_sql) != 0)
  {
    cerr << "*** ERROR: Failed to create config table: " << getLastError() << endl;
    return false;
  }
  
  return true;
} /* MySQLConfigBackend::createTables */

std::string MySQLConfigBackend::escapeString(const std::string& str) const
{
  if (!isOpen() || str.empty())
  {
    return str;
  }
  
  char* escaped = new char[str.length() * 2 + 1];
  unsigned long escaped_length = mysql_real_escape_string(m_mysql, escaped, str.c_str(), str.length());
  
  string result(escaped, escaped_length);
  delete[] escaped;
  
  return result;
} /* MySQLConfigBackend::escapeString */

std::string MySQLConfigBackend::getLastError(void) const
{
  if (m_mysql != nullptr)
  {
    return mysql_error(m_mysql);
  }
  return "MySQL connection not initialized";
} /* MySQLConfigBackend::getLastError */

/*
 * This file has not been truncated
 */
