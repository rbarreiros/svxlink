/**
@file    PttGpiod.cpp
@brief   A PTT hardware controller using a pin in a GPIO port
@author  Tobias Blomberg / SM0SVX
@date    2021-08-13

\verbatim
SvxLink - A Multi Purpose Voice Services System for Ham Radio Use
Copyright (C) 2003-2021 Tobias Blomberg / SM0SVX

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

#include <cstring>
#include <cerrno>
#include <filesystem>
#include <gpiod.hpp>
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

#include "PttGpiodV2.h"



/****************************************************************************
 *
 * Namespaces to use
 *
 ****************************************************************************/



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

PttGpiod::PttGpiod(void)
  : m_line_offset(0)
{
} /* PttGpiod::PttGpiod */


PttGpiod::~PttGpiod(void)
{
} /* PttGpiod::~PttGpiod */


bool PttGpiod::initialize(Async::Config &cfg, const std::string name)
{
  std::filesystem::path chip_path("/dev/gpiochip0");
  std::string chip;
  
  cfg.getValue(name, "PTT_GPIOD_CHIP", chip);
  if(chip.rfind("/dev/", 0)  == 0)
  {
    chip_path = chip;
  } else {
    chip_path = "/dev/" + chip;
  }

  // Check if the GPIO chip path exists and is readable
  if (!std::filesystem::exists(chip_path))
  {
    std::cerr << "*** ERROR: GPIO chip path does not exist: " 
              << chip_path << std::endl;
    return false;
  }

  // Check if it's a character device (typical for GPIO chips)
  if (!std::filesystem::is_character_file(chip_path))
  {
    std::cerr << "*** ERROR: GPIO chip path is not a character device: " 
              << chip_path << std::endl;
    return false;
  }

  // Check if the file is readable
  std::error_code ec;
  auto perms = std::filesystem::status(chip_path, ec).permissions();
  if (ec)
  {
    std::cerr << "*** ERROR: Cannot check permissions for GPIO chip: " 
              << chip_path << " - " << ec.message() << std::endl;
    return false;
  }

  if ((perms & std::filesystem::perms::owner_read) == std::filesystem::perms::none &&
      (perms & std::filesystem::perms::group_read) == std::filesystem::perms::none &&
      (perms & std::filesystem::perms::others_read) == std::filesystem::perms::none)
  {
    std::cerr << "*** ERROR: GPIO chip is not readable: " 
              << chip_path << std::endl;
    return false;
  }

  std::string line;
  if (!cfg.getValue(name, "PTT_GPIOD_LINE", line) || line.empty())
  {
    std::cerr << "*** ERROR: Config variable " << name
              << "/PTT_GPIOD_LINE not set or an illegal value was specified"
              << std::endl;
    return false;
  }

  bool active_low = false;
  if (line[0] == '!')
  {
    active_low = true;
    line.erase(0, 1);
  }

  try
  {
    // Get the line
    int line_num = -1;
    std::istringstream is(line);
    is >> line_num;
    
    if (!is.fail() && is.eof())
    {
      // Line is a number
      m_line_offset = line_num;
    }
    else
    {
      // Find line by name
      int line_num = ::gpiod::chip(chip_path)
                      .get_line_offset_from_name(line);

      if(line_num > 0)
        m_line_offset = line_num;
    }

    if (line_num == -1)
    {
      std::cerr << "*** ERROR: Get GPIOD line \"" << line
                << "\" failed for TX " << name << ": "
                << std::strerror(errno) << std::endl;
      return false;
    }

    std::cout << "Setting up GPIOD with : " << std::endl
              << "Chip: " << chip_path << std::endl
              << "Line: " << line << std::endl
              << "Line offset: " << m_line_offset << std::endl
              << "Active low: " << active_low << std::endl;

    auto lr = ::gpiod::chip(chip)
        .prepare_request()
        .set_consumer("SvxLink")
        .add_line_settings(
          m_line_offset,
          ::gpiod::line_settings()
            .set_direction(
              ::gpiod::line::direction::OUTPUT
            )
            .set_active_low(active_low)
        )
        .do_request();

    m_line_request = &lr;
  }
  catch (const std::exception& e)
  {
    std::cerr << "*** ERROR: GPIOD operation failed for TX " << name 
              << ": " << e.what() << std::endl;
    return false;
  }

  return true;
} /* PttGpiod::initialize */


bool PttGpiod::setTxOn(bool tx_on)
{
  if (!m_line_request)
  {
    std::cerr << "*** WARNING: PttGpiod::setTxOn: "
                 "line request not initialized" << std::endl;
    return false;
  }

  try
  {
    std::cout << "Setting GPIOD line " << m_line_offset << " to " << (tx_on ? "ACTIVE" : "INACTIVE") << std::endl;

    //m_line_request->set_value(m_line_offset, tx_on ? 
    //  ::gpiod::line::value::ACTIVE : ::gpiod::line::value::INACTIVE);
    m_line_request->set_value(7, ::gpiod::line::value::ACTIVE);
  }
  catch (const std::exception& e)
  {
    std::cerr << "*** WARNING: PttGpiod::setTxOn: "
                 "gpiod_line_set_value failed: "
              << e.what() << std::endl;
    return false;
  }

  return true;
} /* PttGpiod::setTxOn */



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



/*
 * This file has not been truncated
 */
