/**
@file	 AsyncConfig.cpp
@brief   A class for reading "INI-foramtted" configuration files
@author  Tobias Blomberg / SM0SVX
@date	 2004-03-17

This file contains a class that is used to read configuration files that is
in the famous MS Windows INI file format. An example of a configuration file
is shown below.

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

#include <iostream>
#include <cassert>
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

#include "AsyncConfig.h"
#include "AsyncConfigFactory.h"
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
  
  // Initialize backend using db.conf
  m_backend = manager.initializeBackend(config_dir);
  if (m_backend == nullptr)
  {
    cerr << "*** FATAL ERROR: " << manager.getLastError() << endl;
    cerr << "*** APPLICATION ABORTING: Cannot initialize configuration backend" << endl;
    exit(1);  // Abort application as requested
  }

  // Load all configuration data into memory for subscription support
  loadFromBackend();

  return true;
} /* Config::open */

bool Config::openDirect(const string& source)
{
  // Create the appropriate backend using the factory (legacy method)
  m_backend = ConfigFactory::createBackend(source);
  if (m_backend == nullptr)
  {
    cerr << "*** ERROR: Failed to create configuration backend for: " << source << endl;
    return false;
  }

  cout << "Using " << m_backend->getBackendType() << " configuration backend: " 
       << m_backend->getBackendInfo() << endl;

  // Load all configuration data into memory for subscription support
  loadFromBackend();

  return true;
} /* Config::openDirect */


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
    valueUpdated(section, tag);
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


/*
 * This file has not been truncated
 */
