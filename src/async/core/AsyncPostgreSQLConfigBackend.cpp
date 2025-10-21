/**
@file	 AsyncPostgreSQLConfigBackend.cpp
@brief   PostgreSQL-based configuration backend implementation
@author  Rui Barreiros
@date	 2025-09-19

This file contains the implementation of the PostgreSQL-based configuration backend.

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

#include <libpq-fe.h>
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

#include "AsyncPostgreSQLConfigBackend.h"

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

#ifdef HAS_POSTGRESQL_SUPPORT
// Factory registration for PostgreSQL backend
static ConfigBackendSpecificFactory<PostgreSQLConfigBackend> postgresql_factory("postgresql");
#endif

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

PostgreSQLConfigBackend::PostgreSQLConfigBackend(void)
  : ConfigBackend(true, 300000), m_conn(nullptr), m_last_check_time("1970-01-01 00:00:00")
{
} /* PostgreSQLConfigBackend::PostgreSQLConfigBackend */

PostgreSQLConfigBackend::~PostgreSQLConfigBackend(void)
{
  close();
} /* PostgreSQLConfigBackend::~PostgreSQLConfigBackend */

bool PostgreSQLConfigBackend::open(const string& source)
{
  close();
  
  m_connection_string = source;
  parseConnectionInfo(source);
  
  m_conn = PQconnectdb(source.c_str());
  
  if (PQstatus(m_conn) != CONNECTION_OK)
  {
    cerr << "*** ERROR: Failed to connect to PostgreSQL database: " << getLastError() << endl;
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
} /* PostgreSQLConfigBackend::open */

void PostgreSQLConfigBackend::close(void)
{
  if (m_conn != nullptr)
  {
    PQfinish(m_conn);
    m_conn = nullptr;
  }
  m_connection_string.clear();
  m_connection_info.clear();
} /* PostgreSQLConfigBackend::close */

bool PostgreSQLConfigBackend::isOpen(void) const
{
  return (m_conn != nullptr && PQstatus(m_conn) == CONNECTION_OK);
} /* PostgreSQLConfigBackend::isOpen */

bool PostgreSQLConfigBackend::getValue(const std::string& section, const std::string& tag,
                                       std::string& value) const
{
  if (!isOpen())
  {
    return false;
  }
  
  const char* params[2] = { section.c_str(), tag.c_str() };
  
  PGresult* result = PQexecParams(m_conn,
                                  "SELECT value FROM config WHERE section = $1 AND tag = $2",
                                  2,      // number of parameters
                                  nullptr, // parameter types (NULL = infer)
                                  params, // parameter values
                                  nullptr, // parameter lengths (NULL = strings)
                                  nullptr, // parameter formats (NULL = text)
                                  0);     // result format (0 = text)
  
  if (PQresultStatus(result) != PGRES_TUPLES_OK)
  {
    cerr << "*** ERROR: Failed to execute SELECT query: " << PQerrorMessage(m_conn) << endl;
    PQclear(result);
    return false;
  }
  
  int nrows = PQntuples(result);
  if (nrows > 0)
  {
    char* result_value = PQgetvalue(result, 0, 0);
    if (result_value != nullptr)
    {
      value = result_value;
    }
    else
    {
      value.clear();
    }
    PQclear(result);
    return true;
  }
  
  PQclear(result);
  return false;
} /* PostgreSQLConfigBackend::getValue */

bool PostgreSQLConfigBackend::setValue(const std::string& section, const std::string& tag,
                                       const std::string& value)
{
  if (!isOpen())
  {
    return false;
  }
  
  const char* params[3] = { section.c_str(), tag.c_str(), value.c_str() };
  
  PGresult* result = PQexecParams(m_conn,
                                  "INSERT INTO config (section, tag, value) VALUES ($1, $2, $3) "
                                  "ON CONFLICT (section, tag) DO UPDATE SET "
                                  "value = EXCLUDED.value, updated_at = CURRENT_TIMESTAMP",
                                  3,      // number of parameters
                                  nullptr, // parameter types (NULL = infer)
                                  params, // parameter values
                                  nullptr, // parameter lengths (NULL = strings)
                                  nullptr, // parameter formats (NULL = text)
                                  0);     // result format (0 = text)
  
  if (PQresultStatus(result) != PGRES_COMMAND_OK)
  {
    cerr << "*** ERROR: Failed to execute INSERT/UPDATE query: " << PQerrorMessage(m_conn) << endl;
    PQclear(result);
    return false;
  }
  
  PQclear(result);
  notifyValueChanged(section, tag, value);
  return true;
} /* PostgreSQLConfigBackend::setValue */

list<string> PostgreSQLConfigBackend::listSections(void) const
{
  list<string> sections;
  
  if (!isOpen())
  {
    return sections;
  }
  
  PGresult* result = executeQueryWithResult("SELECT DISTINCT section FROM config ORDER BY section");
  if (result == nullptr)
  {
    return sections;
  }
  
  int nrows = PQntuples(result);
  for (int i = 0; i < nrows; i++)
  {
    char* section = PQgetvalue(result, i, 0);
    if (section != nullptr)
    {
      sections.push_back(section);
    }
  }
  
  PQclear(result);
  return sections;
} /* PostgreSQLConfigBackend::listSections */

list<string> PostgreSQLConfigBackend::listSection(const string& section) const
{
  list<string> tags;
  
  if (!isOpen())
  {
    return tags;
  }
  
  const char* params[1] = { section.c_str() };
  
  PGresult* result = PQexecParams(m_conn,
                                  "SELECT tag FROM config WHERE section = $1 ORDER BY tag",
                                  1,      // number of parameters
                                  nullptr, // parameter types (NULL = infer)
                                  params, // parameter values
                                  nullptr, // parameter lengths (NULL = strings)
                                  nullptr, // parameter formats (NULL = text)
                                  0);     // result format (0 = text)
  
  if (PQresultStatus(result) != PGRES_TUPLES_OK)
  {
    cerr << "*** ERROR: Failed to execute SELECT tags query: " << PQerrorMessage(m_conn) << endl;
    PQclear(result);
    return tags;
  }
  
  int nrows = PQntuples(result);
  for (int i = 0; i < nrows; i++)
  {
    char* tag = PQgetvalue(result, i, 0);
    if (tag != nullptr)
    {
      tags.push_back(tag);
    }
  }
  
  PQclear(result);
  return tags;
} /* PostgreSQLConfigBackend::listSection */

std::string PostgreSQLConfigBackend::getBackendType(void) const
{
  return "postgresql";
} /* PostgreSQLConfigBackend::getBackendType */

std::string PostgreSQLConfigBackend::getBackendInfo(void) const
{
  return m_connection_info;
} /* PostgreSQLConfigBackend::getBackendInfo */

bool PostgreSQLConfigBackend::checkForExternalChanges(void)
{
  if (!isOpen())
  {
    return false;
  }

  // Query for all records updated since last check
  const char* params[1] = { m_last_check_time.c_str() };
  
  PGresult* result = PQexecParams(m_conn,
                                  "SELECT section, tag, value, updated_at FROM config "
                                  "WHERE updated_at > $1 ORDER BY updated_at",
                                  1,      // number of parameters
                                  nullptr, // parameter types (NULL = infer)
                                  params, // parameter values
                                  nullptr, // parameter lengths (NULL = strings)
                                  nullptr, // parameter formats (NULL = text)
                                  0);     // result format (0 = text)

  if (PQresultStatus(result) != PGRES_TUPLES_OK)
  {
    cerr << "*** ERROR: Failed to execute change detection query: " << PQerrorMessage(m_conn) << endl;
    PQclear(result);
    return false;
  }

  bool changes_detected = false;
  std::string latest_timestamp = m_last_check_time;
  int rows = PQntuples(result);

  for (int i = 0; i < rows; i++)
  {
    const char* section = PQgetvalue(result, i, 0);
    const char* tag = PQgetvalue(result, i, 1);
    const char* value = PQgetvalue(result, i, 2);
    const char* updated_at = PQgetvalue(result, i, 3);

    if (section && tag && value && updated_at)
    {
      notifyValueChanged(std::string(section), std::string(tag), std::string(value));
      latest_timestamp = std::string(updated_at);
      changes_detected = true;
    }
  }

  PQclear(result);

  // Update last check time to the latest timestamp seen
  if (changes_detected)
  {
    m_last_check_time = latest_timestamp;
  }

  return changes_detected;
} /* PostgreSQLConfigBackend::checkForExternalChanges */

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

bool PostgreSQLConfigBackend::createTables(void)
{
  const char* create_table_sql = 
    "CREATE TABLE IF NOT EXISTS config ("
    "  id SERIAL PRIMARY KEY,"
    "  section VARCHAR(255) NOT NULL,"
    "  tag VARCHAR(255) NOT NULL,"
    "  value TEXT NOT NULL,"
    "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
    "  updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
    "  UNIQUE(section, tag)"
    ")";
  
  if (!executeQuery(create_table_sql))
  {
    return false;
  }
  
  // Create index for faster lookups
  const char* create_index_sql = 
    "CREATE INDEX IF NOT EXISTS idx_config_section ON config(section)";
  
  if (!executeQuery(create_index_sql))
  {
    return false;
  }
  
  // Create trigger to update updated_at timestamp
  const char* create_trigger_function = 
    "CREATE OR REPLACE FUNCTION update_updated_at_column() "
    "RETURNS TRIGGER AS $$ "
    "BEGIN "
    "  NEW.updated_at = CURRENT_TIMESTAMP; "
    "  RETURN NEW; "
    "END; "
    "$$ language 'plpgsql'";
  
  if (!executeQuery(create_trigger_function))
  {
    return false;
  }
  
  const char* create_trigger = 
    "DROP TRIGGER IF EXISTS update_config_updated_at ON config; "
    "CREATE TRIGGER update_config_updated_at "
    "  BEFORE UPDATE ON config "
    "  FOR EACH ROW "
    "  EXECUTE FUNCTION update_updated_at_column()";
  
  if (!executeQuery(create_trigger))
  {
    return false;
  }
  
  return true;
} /* PostgreSQLConfigBackend::createTables */

bool PostgreSQLConfigBackend::executeQuery(const std::string& query) const
{
  if (!isOpen())
  {
    return false;
  }
  
  PGresult* result = PQexec(m_conn, query.c_str());
  
  if (PQresultStatus(result) != PGRES_COMMAND_OK)
  {
    cerr << "*** ERROR: Failed to execute query: " << PQerrorMessage(m_conn) << endl;
    PQclear(result);
    return false;
  }
  
  PQclear(result);
  return true;
} /* PostgreSQLConfigBackend::executeQuery */

PGresult* PostgreSQLConfigBackend::executeQueryWithResult(const std::string& query) const
{
  if (!isOpen())
  {
    return nullptr;
  }
  
  PGresult* result = PQexec(m_conn, query.c_str());
  
  if (PQresultStatus(result) != PGRES_TUPLES_OK)
  {
    cerr << "*** ERROR: Failed to execute query: " << PQerrorMessage(m_conn) << endl;
    PQclear(result);
    return nullptr;
  }
  
  return result;
} /* PostgreSQLConfigBackend::executeQueryWithResult */

std::string PostgreSQLConfigBackend::escapeString(const std::string& str) const
{
  if (!isOpen() || str.empty())
  {
    return str;
  }
  
  char* escaped = new char[str.length() * 2 + 1];
  int error;
  size_t escaped_length = PQescapeStringConn(m_conn, escaped, str.c_str(), str.length(), &error);
  
  if (error != 0)
  {
    delete[] escaped;
    return str; // Return original string if escaping failed
  }
  
  string result(escaped, escaped_length);
  delete[] escaped;
  
  return result;
} /* PostgreSQLConfigBackend::escapeString */

std::string PostgreSQLConfigBackend::getLastError(void) const
{
  if (m_conn != nullptr)
  {
    return PQerrorMessage(m_conn);
  }
  return "PostgreSQL connection not initialized";
} /* PostgreSQLConfigBackend::getLastError */

void PostgreSQLConfigBackend::parseConnectionInfo(const std::string& conn_str)
{
  // Create a sanitized version of the connection string without password
  istringstream iss(conn_str);
  ostringstream sanitized;
  string token;
  bool first = true;
  
  while (iss >> token)
  {
    if (token.find("password=") == 0)
    {
      // Skip password parameter
      continue;
    }
    
    if (!first)
    {
      sanitized << " ";
    }
    first = false;
    sanitized << token;
  }
  
  m_connection_info = sanitized.str();
} /* PostgreSQLConfigBackend::parseConnectionInfo */

/*
 * This file has not been truncated
 */
