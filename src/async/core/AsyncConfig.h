/**
@file	 AsyncConfig.h
@brief   A class for configuration handling
@author  Tobias Blomberg
@date	 2004-03-17

This file contains a class that is used to supply and save configuration data
to a backend which can be a file, a database, etc. The backend can be
implemented by extending AsyncConfigBackend.

\include test.cfg

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

/** @example AsyncConfig_demo.cpp
An example of how to use the Config class
*/


#ifndef ASYNC_CONFIG_INCLUDED
#define ASYNC_CONFIG_INCLUDED


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <stdio.h>
#include <sigc++/sigc++.h>

#include <string>
#include <map>
#include <list>
#include <memory>
#include <sstream>
#include <locale>
#include <vector>
#include <cassert>


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
#include "AsyncConfigManager.h"



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
@brief	A class for reading INI-formatted configuration files
@author Tobias Blomberg
@date   2004-03-17

This class is used to read configuration files that is in the famous MS Windows
INI file format. An example of a configuration file and how to use the class
is shown below.

\include test.cfg

\include AsyncConfig_demo.cpp
*/
class Config
{
  public:
    /**
     * @brief 	Default constuctor
     */
    Config(void) {}

    /**
     * @brief 	Copy constructor (deleted - Config is not copyable)
     */
    Config(const Config&) = delete;

    /**
     * @brief 	Assignment operator (deleted - Config is not assignable)
     */
    Config& operator=(const Config&) = delete;
  
    /**
     * @brief 	Destructor
     */
    ~Config(void);
  
    /**
     * @brief 	Open configuration using db.conf for backend selection
     * @param 	config_dir Optional configuration directory to search for db.conf
     * @return	Returns \em true on success or else \em false.
     *
     * This function reads db.conf to determine which configuration backend to use.
     * It searches for db.conf in the following locations:
     * 1. config_dir/db.conf (if config_dir is provided)
     * 2. ~/.svxlink/db.conf
     * 3. /etc/svxlink/db.conf
     * 4. SVX_SYSCONF_INSTALL_DIR/db.conf
     * 
     * If no db.conf is found, defaults to file backend with svxlink.conf.
     * The application will abort with an error if:
     * - The specified backend is not available (not compiled in)
     * - Database connection fails
     * - Configuration cannot be loaded
     */
    bool open(const std::string& config_dir = "");

    /**
     * @brief 	Open configuration using specific db.conf file
     * @param 	db_conf_path The full path to the db.conf file to use
     * @return	Returns \em true on success or else \em false.
     *
     * This function directly uses the specified db.conf file for backend
     * selection instead of searching in standard locations.
     */
    bool openFromDbConfig(const std::string& db_conf_path);

    /**
     * @brief 	Open configuration with explicit source (legacy method)
     * @param 	source The configuration source (file path, database URL, etc.)
     * @return	Returns \em true on success or else \em false.
     *
     * This is the legacy method that directly opens a configuration source.
     * For new applications, prefer the parameterless open() method that
     * uses db.conf for backend selection.
     */
    bool openDirect(const std::string& source);

    /**
     * @brief 	Get the main configuration file path
     * @return	Returns the path to the main configuration file, or empty if using database backend
     *
     * This method returns the path to the main configuration file that was loaded.
     * For file backend, this is the path to the main .conf file.
     * For database backends, this returns an empty string since there's no single file.
     */
    std::string getMainConfigFile(void) const;

    /**
     * @brief   Get the configuration backend type
     * @return  Returns the backend type string ("file", "sqlite", "mysql", "postgresql")
     *
     * This method returns the type of configuration backend currently in use.
     */
    std::string getBackendType(void) const;

    /**
     * @brief   Get direct access to the configuration backend
     * @return  Pointer to the ConfigBackend or nullptr if not open
     *
     * This provides direct access to the backend for advanced operations
     * like enabling change notifications or starting auto-polling.
     */
    ConfigBackend* getBackend(void);

    /**
     * @brief   Reload the configuration from its source
     *
     * This method forces a reload of all configuration values from the backend.
     * For database backends, it calls checkForExternalChanges() first.
     * After reloading, it triggers all subscribeValue callbacks for values that changed.
     */
    void reload(void);

    /**
     * @brief Result structure for openWithFallback()
     */
    struct ConfigLoadResult
    {
      bool success;                  ///< Whether config was successfully loaded
      std::string source_type;       ///< "command_line", "dbconfig", "default", or "none"
      std::string source_path;       ///< Path to the config file or db.conf
      std::string backend_type;      ///< Backend type used (file/sqlite/mysql/postgresql)
      std::string error_message;     ///< Error message if success == false
      bool used_dbconfig;            ///< True if configuration came from db.conf
    };

    /**
     * @brief   Smart configuration initialization with fallback
     * @param   cmdline_config Path from --config option (empty if not provided)
     * @param   cmdline_dbconfig Path from --dbconfig option (empty if not provided)
     * @param   default_config_name Default config filename (e.g., "svxlink.conf")
     * @return  ConfigLoadResult with detailed information
     *
     * This method implements a complete configuration initialization cascade:
     * 1. If --config provided: use openDirect() with that file
     * 2. If --dbconfig provided: use openFromDbConfig() with that file
     * 3. Search for db.conf in standard locations, use if found
     * 4. Search for default_config_name in standard locations, use if found
     * 5. Fail with detailed error information
     *
     * Example usage:
     * @code
     * Async::Config cfg;
     * auto result = cfg.openWithFallback(config_arg, dbconfig_arg, "svxlink.conf");
     * if (!result.success) {
     *   cerr << "*** ERROR: " << result.error_message << endl;
     *   return 1;
     * }
     * cout << "Configuration loaded from: " << result.source_path << endl;
     * @endcode
     */
    ConfigLoadResult openWithFallback(const std::string& cmdline_config,
                                      const std::string& cmdline_dbconfig,
                                      const std::string& default_config_name);
    
    /**
     * @brief 	Return the string value of the given configuration variable
     * @param 	section The name of the section where the configuration
     *	      	      	variable is located
     * @param 	tag   	The name of the configuration variable to get
     * @return	Returns String with content of the configuration variable.
     *                  If no variable is found an empty string is returned
     *
     * This function will return the string value corresponding to the given
     * configuration variable. If the configuration variable is unset, an
     * empty sting is returned.
     */
    const std::string &getValue(const std::string& section,
				 const std::string& tag) const;

    /**
     * @brief 	Get the string value of the given configuration variable
     * @param 	section    The name of the section where the configuration
     *	      	      	   variable is located
     * @param 	tag   	   The name of the configuration variable to get
     * @param 	value 	   The value is returned in this argument. Any previous
     *	      	      	   contents is wiped
     * @return	Returns \em true on success or else \em false on failure
     *
     * This function is used to get the value for a configuration variable
     * of type "string".
     */
    bool getValue(const std::string& section, const std::string& tag,
                  std::string& value, bool missing_ok = false) const;

    /**
     * @brief 	Get the char value of the given configuration variable
     * @param 	section    The name of the section where the configuration
     *	      	      	   variable is located
     * @param 	tag   	   The name of the configuration variable to get
     * @param 	value 	   The value is returned in this argument. Any previous
     *	      	      	   contents is wiped
     * @return	Returns \em true on success or else \em false on failure
     *
     * This function is used to get the value for a configuration variable
     * of type "char". It is an error if the size of the value is anything but
     * 1 byte.
     */
    bool getValue(const std::string& section, const std::string& tag,
                  char& value, bool missing_ok = false) const;
    /**
     * @brief 	Get the value of the given configuration variable.
     * @param 	section    The name of the section where the configuration
     *	      	      	   variable is located
     * @param 	tag   	   The name of the configuration variable to get
     * @param 	rsp 	   The value is returned in this argument.
     *	      	      	   Successful completion overwrites previous contents
     * @param	missing_ok If set to \em true, return \em true if the
     *                     configuration variable is missing
     * @return	Returns \em true on success or else \em false on failure.
     *
     * This function is used to get the value of a configuraiton variable.
     * It's a template function meaning that it can take any value type
     * that supports the operator>> function. Note that when the value is of
     * type string, the overloaded getValue is used rather than this function.
     * Normally a missing configuration variable is seen as an error and the
     * function returns \em false. If the missing_ok parameter is set to
     * \em true, this function returns \em true for a missing variable but
     * still returns \em false if an illegal value is specified.
     */
    template <typename Rsp>
    bool getValue(const std::string& section, const std::string& tag,
		  Rsp &rsp, bool missing_ok = false) const
    {
      std::string str_val;
      if (!getValue(section, tag, str_val))
      {
	return missing_ok;
      }
      std::stringstream ssval(str_val);
      Rsp tmp;
      ssval >> tmp;
      if(!ssval.eof())
      {
        ssval >> std::ws;
      }
      if (ssval.fail() || !ssval.eof())
      {
	return false;
      }
      rsp = tmp;
      return true;
    } /* Config::getValue */

    /**
     * @brief 	Get the value of the given config variable into container
     * @param 	section    The name of the section where the configuration
     *	      	      	   variable is located
     * @param 	tag   	   The name of the configuration variable to get
     * @param 	c 	   The value is returned in this argument.
     *	      	      	   Successful completion overwrites previous contents
     * @param	missing_ok If set to \em true, return \em true if the
     *                     configuration variable is missing
     * @return	Returns \em true on success or else \em false on failure.
     *
     * This function is used to get the value of a configuraiton variable.
     * The config variable is read into a container (e.g. vector, list etc).
     * It's a template function meaning that it can take any value type
     * that supports the operator>> function. 
     * Normally a missing configuration variable is seen as an error and the
     * function returns \em false. If the missing_ok parameter is set to
     * \em true, this function returns \em true for a missing variable but
     * still returns \em false if an illegal value is specified.
     */
    template <template <typename, typename> class Container,
              typename Value>
    bool getValue(const std::string& section, const std::string& tag,
		  Container<Value, std::allocator<Value> > &c,
                  bool missing_ok = false) const
    {
      std::string str_val;
      if (!getValue(section, tag, str_val))
      {
	return missing_ok;
      }
      if (str_val.empty())
      {
        c.clear();
        return true;
      }
      std::stringstream ssval(str_val);
      ssval.imbue(std::locale(ssval.getloc(), new csv_whitespace));
      while (!ssval.eof())
      {
        Value tmp;
        ssval >> tmp;
        if(!ssval.eof())
        {
          ssval >> std::ws;
        }
        if (ssval.fail())
        {
          return false;
        }
        c.push_back(tmp);
      }
      return true;
    } /* Config::getValue */

    /**
     * @brief 	Get the value of the given config variable into keyed container
     * @param 	section    The name of the section where the configuration
     *	      	      	   variable is located
     * @param 	tag   	   The name of the configuration variable to get
     * @param 	c 	   The value is returned in this argument.
     *	      	      	   Successful completion overwrites previous contents
     * @param	missing_ok If set to \em true, return \em true if the
     *                     configuration variable is missing
     * @return	Returns \em true on success or else \em false on failure.
     *
     * This function is used to get the value of a configuraiton variable.
     * The config variable is read into a keyed container (e.g. set, multiset
     * etc).
     * It's a template function meaning that it can take any key type
     * that supports the operator>> function.
     * Normally a missing configuration variable is seen as an error and the
     * function returns \em false. If the missing_ok parameter is set to
     * \em true, this function returns \em true for a missing variable but
     * still returns \em false if an illegal value is specified.
     */
    template <template <typename, typename, typename> class Container,
              typename Key>
    bool getValue(const std::string& section, const std::string& tag,
                  Container<Key, std::less<Key>, std::allocator<Key> > &c,
                  bool missing_ok = false) const
    {
      std::string str_val;
      if (!getValue(section, tag, str_val))
      {
        return missing_ok;
      }
      if (str_val.empty())
      {
        c.clear();
        return true;
      }
      std::stringstream ssval(str_val);
      ssval.imbue(std::locale(ssval.getloc(), new csv_whitespace));
      while (!ssval.eof())
      {
        Key tmp;
        ssval >> tmp;
        if(!ssval.eof())
        {
          ssval >> std::ws;
        }
        if (ssval.fail())
        {
          return false;
        }
        c.insert(tmp);
      }
      return true;
    } /* Config::getValue */

    /**
     * @brief   Get value of given config variable into associative container
     * @param   section    The name of the section where the configuration
     *                     variable is located
     * @param   tag        The name of the configuration variable to get
     * @param   c          The value is returned in this argument.
     *                     Successful completion overwrites previous contents
     * @param   sep        The character used to separate key and value
     * @param   missing_ok If set to \em true, return \em true if the
     *                     configuration variable is missing
     * @return  Returns \em true on success or else \em false on failure.
     *
     * This function is used to get the value of a configuraiton variable.  The
     * config variable is read into an associative container (e.g. std::map or
     * std::multimap).  It's a template function meaning that it can take any
     * key and value type that supports the operator>> function.
     * Normally a missing configuration variable is seen as an error and the
     * function returns \em false. If the missing_ok parameter is set to \em
     * true, this function returns \em true for a missing variable but still
     * returns \em false if an illegal value is specified.
     */
    template <template <typename, typename, typename, typename> class Container,
              class Key, class T, class Compare=std::less<Key>,
              class Allocator=std::allocator<std::pair<const Key, T>>>
    bool getValue(const std::string& section, const std::string& tag,
                  Container<Key, T, Compare, Allocator>& c,
                  char sep = ':', bool missing_ok = false) const
    {
      std::string str_val;
      if (!getValue(section, tag, str_val))
      {
        return missing_ok;
      }
      if (str_val.empty())
      {
        c.clear();
        return true;
      }
      std::stringstream ssval(str_val);
      ssval.imbue(std::locale(ssval.getloc(), new csv_whitespace));
      while (!ssval.eof())
      {
        std::string entry;
        ssval >> entry;
        std::string::size_type seppos = entry.find(sep);
        if (seppos == std::string::npos)
        {
          return false;
        }
        std::string keystr(entry.substr(0, seppos));
        std::string valuestr(entry.substr(seppos+1));
        Key key;
        T value;
        if (!setValueFromString(key, keystr) ||
           !setValueFromString(value, valuestr))
        {
          return false;
        }
        if(!ssval.eof())
        {
          ssval >> std::ws;
        }
        if (ssval.fail())
        {
          return false;
        }
        c.insert(std::pair<Key, T>(key, value));
      }
      return true;
    } /* Config::getValue */

    /**
     * @brief 	Get a range checked variable value
     * @param 	section    The name of the section where the configuration
     *	      	      	   variable is located
     * @param 	tag   	   The name of the configuration variable to get.
     * @param 	min   	   Smallest valid value.
     * @param 	max   	   Largest valid value.
     * @param 	rsp 	   The value is returned in this argument.
     *	      	      	   Successful completion overwites prevoius contents.
     * @param	missing_ok If set to \em true, return \em true if the
     *                     configuration variable is missing
     * @return	Returns \em true if value is within range otherwise \em false.
     *
     * This function is used to get the value of the given configuration
     * variable, checking if it is within the given range (min <= value <= max).
     * Requires operators >>, < and > to be defined in the value object.
     * Normally a missing configuration variable is seen as an error and the
     * function returns \em false. If the missing_ok parameter is set to
     * \em true, this function returns \em true for a missing variable but
     * till returns \em false if an illegal value is specified.
     */
    template <typename Rsp>
    bool getValue(const std::string& section, const std::string& tag,
		  const Rsp& min, const Rsp& max, Rsp &rsp,
		  bool missing_ok = false) const
    {
      std::string str_val;
      if (!getValue(section, tag, str_val))
      {
	return missing_ok;
      }
      std::stringstream ssval(str_val);
      Rsp tmp;
      ssval >> tmp;
      if(!ssval.eof())
      {
        ssval >> std::ws;
      }
      if (ssval.fail() || !ssval.eof() || (tmp < min) || (tmp > max))
      {
	return false;
      }
      rsp = tmp;
      return true;
    } /* Config::getValue */

    /**
     * @brief Subscribe to the given configuration variable (char*)
     * @param section The name of the section where the configuration
     *                variable is located
     * @param tag     The name of the configuration variable to get
     * @param def     Default value if the config var does not exist
     * @param func    The function to call when the config var changes
     *
     * This function is used to subscribe to the changes of the specified
     * configuration variable. The given function will be called when the value
     * changes. If the configuration variable is not set, it will be set to the
     * given default value.
     *
     * This version of the function is called when the default value is a C
     * string (char*).
     */
    template <typename F=std::function<void(const char*)>>
    void subscribeValue(const std::string& section, const std::string& tag,
                        const char* def, F func)
    {
      subscribeValue(section, tag, std::string(def),
          [=](const std::string& str_val) -> void
          {
            func(str_val.c_str());
          });
    } /* subscribeValue */

    /**
     * @brief Subscribe to the given configuration variable
     * @param section The name of the section where the configuration
     *                variable is located
     * @param tag     The name of the configuration variable to get
     * @param def     Default value if the config var does not exist
     * @param func    The function to call when the config var changes
     *
     * This function is used to subscribe to the changes of the specified
     * configuration variable. The given function will be called when the value
     * changes. If the configuration variable is not set, it will be set to the
     * given default value.
     *
     * This version of the function is called when the default value is of a
     * non-container type (e.g. std::string, int, bool etc).
     */
    template <typename Rsp, typename F=std::function<void(const Rsp&)>>
    void subscribeValue(const std::string& section, const std::string& tag,
                        const Rsp& def, F func)
    {
      Value& v = getValueP(section, tag, def);
      v.subs.push_back(
          [=](const std::string& str_val) -> void
          {
            std::stringstream ssval(str_val);
            ssval.imbue(std::locale(ssval.getloc(), new empty_ctype));
            Rsp tmp;
            ssval >> tmp;
            func(tmp);
          });
      v.subs.back()(v.val);
    } /* subscribeValue */

    /**
     * @brief Subscribe to the given configuration variable (sequence)
     * @param section The name of the section where the configuration
     *                variable is located
     * @param tag     The name of the configuration variable to get
     * @param def     Default value if the config var does not exist
     * @param func    The function to call when the config var changes
     *
     * This function is used to subscribe to the changes of the specified
     * configuration variable. The given function will be called when the value
     * changes. If the configuration variable is not set, it will be set to the
     * given default value.
     *
     * This version of the function is called when the default value is a
     * sequence container (e.g. std::vector, std::list etc).
     */
    template <template <typename, typename> class Container,
              typename Rsp, typename F=std::function<void(const Rsp&)>>
    void subscribeValue(const std::string& section, const std::string& tag,
                        const Container<Rsp, std::allocator<Rsp>>& def, F func)
    {
      Value& v = getValueP(section, tag, def);
      v.subs.push_back(
          [=](const std::string& str_val) -> void
          {
            std::stringstream ssval(str_val);
            ssval.imbue(std::locale(ssval.getloc(), new csv_whitespace));
            Container<Rsp, std::allocator<Rsp>> c;
            while (!ssval.eof())
            {
              Rsp tmp;
              ssval >> tmp;
              if(!ssval.eof())
              {
                ssval >> std::ws;
              }
              if (ssval.fail())
              {
                return;
              }
              c.push_back(tmp);
            }
            func(std::move(c));
          });
      v.subs.back()(v.val);
    } /* Config::subscribeValue */

    /**
     * @brief   Return the name of all configuration sections
     * @return  Returns a list of all existing section names
     */
    std::list<std::string> listSections(void);

    /**
     * @brief 	Return the name of all the tags in the given section
     * @param 	section The name of the section where the configuration
     *	      	      	variables are located
     * @return	Returns the list of tags in the given section
     */
    std::list<std::string> listSection(const std::string& section);

    /**
     * @brief   Set the value of a configuration variable
     * @param 	section   The name of the section where the configuration
     *	      	      	  variable is located
     * @param 	tag   	  The name of the configuration variable to set.
     * @param   value     The value to set
     *
     * This function is used to set the value of a configuration variable.
     * If the given configuration section or variable does not exist, it
     * is created.
     * Note that this function will not write anything back to the
     * associated configuration file. It will only set the value in memory.
     *
     * The valueUpdated signal will be emitted so that subscribers can get
     * notified when the value of a configuration variable is changed.
     */
    void setValue(const std::string& section, const std::string& tag,
      	      	  const std::string& value);

    /**
     * @brief   Set the value of a configuration variable (generic type)
     * @param   section   The name of the section where the configuration
     *                    variable is located
     * @param   tag       The name of the configuration variable to set.
     * @param   value     The value to set
     *
     * This function is used to set the value of a configuration variable.
     * The type of the value may be any type that support streaming to string.
     * If the given configuration section or variable does not exist, it
     * is created.
     * Note that this function will not write anything back to the
     * associated configuration file. It will only set the value in memory.
     *
     * The valueUpdated signal will be emitted so that subscribers can get
     * notified when the value of a configuration variable is changed.
     */
    template <typename Rsp>
    void setValue(const std::string& section, const std::string& tag,
                  const Rsp& value)
    {
      std::ostringstream ss;
      ss << value;
      setValue(section, tag, ss.str());
    }

    /**
     * @brief   Set the value of a configuration variable (sequence container)
     * @param   section   The name of the section where the configuration
     *                    variable is located
     * @param   tag       The name of the configuration variable to set.
     * @param   c         The sequence to set
     *
     * This function is used to set the value of a configuration variable that
     * holds a sequence container (e.g. std::vector, std::list etc).
     * The type of the elements of the container may be any type that support
     * streaming to string.
     * If the given configuration section or variable does not exist, it
     * is created.
     * Note that this function will not write anything back to the
     * associated configuration file. It will only set the value in memory.
     *
     * The valueUpdated signal will be emitted so that subscribers can get
     * notified when the value of a configuration variable is changed.
     */
    template <template <typename, typename> class Container,
              typename Rsp>
    void setValue(const std::string& section, const std::string& tag,
                  const Container<Rsp, std::allocator<Rsp>>& c)
    {
      std::ostringstream ss;
      bool first_val = true;
      for (const auto& val : c)
      {
        if (!first_val)
        {
          ss << ",";
        }
        first_val = false;
        ss << val;
      }
      setValue(section, tag, ss.str());
    } /* setValue */

    /**
     * @brief   A signal that is emitted when a config value is updated
     * @param   section The config section of the update
     * @param   tag     The tag (variable name) of the update
     *
     * This signal is emitted whenever a configuration variable is changed
     * by calling the setValue function. It will only be emitted if the value
     * actually changes.
     */
    sigc::signal<void(const std::string&, const std::string&)> valueUpdated;

  private:
    using Subscriber = std::function<void(const std::string&)>;
    struct Value
    {
      std::string             val;
      std::vector<Subscriber> subs;
    };
    typedef std::map<std::string, Value>  Values;
    typedef std::map<std::string, Values> Sections;

      // Really wanted to use classic_table() but it returns nullptr on Alpine
    static const std::ctype<char>::mask* empty_table()
    {
      static const auto table_size = std::ctype<char>::table_size;
      static std::ctype<char>::mask v[table_size];
      std::fill(&v[0], &v[table_size], 0);
      return &v[0];
    }

    struct empty_ctype : std::ctype<char>
    {
      static const mask* make_table(void) { return empty_table(); }
      empty_ctype(std::size_t refs=0) : ctype(make_table(), false, refs) {}
    };

    struct csv_whitespace : std::ctype<char>
    {
      static const mask* make_table()
      {
        auto tbl = empty_table();
        static std::vector<mask> v(tbl, tbl + table_size);
        v[' '] |= space;
        v[','] |= space;
        return &v[0];
      }
      csv_whitespace(std::size_t refs=0) : ctype(make_table(), false, refs) {}
    };

    ConfigBackendPtr m_backend;
    Sections         m_sections;  // In-memory cache for subscriptions
    std::string      m_main_config_file; // Path to main config file (for CFG_DIR resolution)

    void loadFromBackend(void);
    void syncToBackend(const std::string& section, const std::string& tag);
    void onBackendValueChanged(const std::string& section, const std::string& tag, const std::string& value);
    void connectBackendSignals(void);

    template <class T>
    bool setValueFromString(T& val, const std::string &str) const
    {
      std::istringstream ss(str);
      ss >> std::noskipws >> val;
      if(!ss.eof())
      {
        ss >> std::ws;
      }
      return !ss.fail() && ss.eof();
    }

    template <typename T>
    Value& getValueP(const std::string& section, const std::string& tag,
                     const T& def)
    {
      Values::iterator val_it = m_sections[section].find(tag);
      if (val_it == m_sections[section].end())
      {
        setValue(section, tag, def);
      }

      return m_sections[section][tag];
    } /* getValueP */

}; /* class Config */


} /* namespace */

#endif /* ASYNC_CONFIG_INCLUDED */



/*
 * This file has not been truncated
 */

