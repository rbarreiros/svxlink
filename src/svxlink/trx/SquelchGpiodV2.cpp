/**
@file	 SquelchGpiod.cpp
@brief   A squelch detector that read squelch state from a GPIO port pin
@author  Tobias Blomberg / SM0SVX
@date	 2021-08-13

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

#include "SquelchGpiodV2.h"



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

SquelchGpiod::SquelchGpiod(void)
  : m_timer(100, Async::Timer::TYPE_PERIODIC)
{
} /* SquelchGpiod::SquelchGpiod */


SquelchGpiod::~SquelchGpiod(void)
{
  m_timer.setEnable(false);
  
  m_line.reset();
  m_chip.reset();
} /* SquelchGpiod::~SquelchGpiod */


bool SquelchGpiod::initialize(Async::Config& cfg, const std::string& rx_name)
{
  if (!Squelch::initialize(cfg, rx_name))
  {
    return false;
  }

  std::string chip("gpiochip0");
  cfg.getValue(rx_name, "SQL_GPIOD_CHIP", chip);

  std::string line;
  if (!cfg.getValue(rx_name, "SQL_GPIOD_LINE", line) || line.empty())
  {
    std::cerr << "*** ERROR: Config variable " << rx_name
              << "/SQL_GPIOD_LINE not set or an illegal value was specified"
              << std::endl;
    return false;
  }

  bool active_low = false;
  if (line[0] == '!')
  {
    active_low = true;
    line.erase(0, 1);
  }

  std::string bias;
  cfg.getValue(rx_name, "SQL_GPIOD_BIAS", bias);

  try
  {
    // Open the GPIO chip
    m_chip = std::make_unique<gpiod::chip>(chip);
    
    // Get the line
    int line_num = -1;
    std::istringstream is(line);
    is >> line_num;
    
    if (!is.fail() && is.eof())
    {
      // Line specified as number
      m_line = std::make_unique<gpiod::line>(m_chip->get_line(line_num));
    }
    else
    {
      // Line specified as name
      m_line = std::make_unique<gpiod::line>(m_chip->find_line(line));
    }

    // Configure the line for input
    gpiod::line_request_config req_cfg;
    req_cfg.consumer = "SvxLink";
    req_cfg.request_type = gpiod::line_request::DIRECTION_INPUT;
    req_cfg.flags = active_low ? gpiod::line_request::FLAG_ACTIVE_LOW : 0;

    // Handle bias configuration
    if (!bias.empty())
    {
      if (bias == "PULLUP")
      {
        req_cfg.flags |= gpiod::line_request::FLAG_BIAS_PULL_UP;
      }
      else if (bias == "PULLDOWN")
      {
        req_cfg.flags |= gpiod::line_request::FLAG_BIAS_PULL_DOWN;
      }
      else if (bias == "DISABLE")
      {
        req_cfg.flags |= gpiod::line_request::FLAG_BIAS_DISABLE;
      }
      else
      {
        std::cerr << "*** ERROR: Config variable " << rx_name
                  << "/SQL_GPIOD_BIAS has an illegal value specified. "
                     "Valid values are: DISABLE, PULLUP and PULLDOWN."
                  << std::endl;
        return false;
      }
    }

    // Request the line
    m_line->request(req_cfg, 0);

    // Set up periodic timer to read GPIO value
    m_timer.expired.connect([&](Async::Timer*) {
          try
          {
            int val = m_line->get_value();
            setSignalDetected(val > 0);
          }
          catch (const gpiod::exception& e)
          {
            std::cerr << "*** WARNING: Read GPIOD line failed for RX \"" 
                      << rx_name << "\": " << e.what() << std::endl;
          }
          catch (const std::exception& e)
          {
            std::cerr << "*** WARNING: Unexpected error reading GPIOD line for RX \"" 
                      << rx_name << "\": " << e.what() << std::endl;
          }
        });

    m_timer.setEnable(true);
  }
  catch (const gpiod::exception& e)
  {
    std::cerr << "*** ERROR: GPIOD operation failed for RX \"" << rx_name 
              << "\": " << e.what() << std::endl;
    return false;
  }
  catch (const std::exception& e)
  {
    std::cerr << "*** ERROR: Unexpected error for RX \"" << rx_name 
              << "\": " << e.what() << std::endl;
    return false;
  }

  return true;
} /* SquelchGpiod::initialize */


/****************************************************************************
 *
 * Private member functions
 *
 ****************************************************************************/



/*
 * This file has not been truncated
 */
