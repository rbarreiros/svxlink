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
  : m_timer(100, Async::Timer::TYPE_PERIODIC),
  m_line_offset(0)
{
} /* SquelchGpiod::SquelchGpiod */


SquelchGpiod::~SquelchGpiod(void)
{
  m_timer.setEnable(false);
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

    // Get the line
    int line_num = -1;
    std::istringstream is(line);
    is >> line_num;
    
    if (!is.fail() && is.eof())
    {
      // Line is as number
      if(line_num > 0)
        m_line_offset = line_num;
    }
    else
    {
      // Find line by name
      line_num = ::gpiod::chip(chip)
                      .get_line_offset_from_name(line);

      if(line_num > 0)
        m_line_offset = line_num;
    }

    if (line_num == -1) {
      std::cerr << "*** ERROR: Get GPIOD line \"" << line 
                << "\" failed for RX " << rx_name << ": "
                << rx_name << ": " << std::strerror(errno) << std::endl;
      return false;
    }

    // bias
    ::gpiod::line::bias gpiod_bias = ::gpiod::line::bias::UNKNOWN;
    if (!bias.empty())
    {
      if (bias == "PULLUP")
      {
        gpiod_bias = ::gpiod::line::bias::PULL_UP;
      }
      else if (bias == "PULLDOWN")
      {
        gpiod_bias = ::gpiod::line::bias::PULL_DOWN;
      }
      else if (bias == "DISABLE")
      {
        gpiod_bias = ::gpiod::line::bias::DISABLED;
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


    auto lr = ::gpiod::chip(chip)
                .prepare_request()
                .set_consumer("SvxLink")
                .add_line_settings(
                  m_line_offset,
                  ::gpiod::line_settings()
                    .set_direction(
                      ::gpiod::line::direction::INPUT
                    )
                    .set_bias(gpiod_bias)
                    .set_edge_detection(
                      ::gpiod::line::edge::BOTH
                    )
                    .set_debounce_period(
                      std::chrono::milliseconds(10)
                    )
                    .set_active_low(active_low)
                )
                .do_request();

    m_line_request = &lr;

    // Set up periodic timer to read GPIO value
    m_timer.expired.connect([&](Async::Timer*) {
          try
          {
            ::gpiod::line::value val = 
              m_line_request->get_value(m_line_offset);

            setSignalDetected(
              (val == ::gpiod::line::value::ACTIVE) ? 
                true : false);
          }
          catch (const std::exception& e)
          {
            std::cerr << "*** WARNING: Read GPIOD line failed for RX \"" 
                      << rx_name << "\": " << e.what() << std::endl;
          }
        });

    m_timer.setEnable(true);
  }
  catch (const std::exception& e)
  {
    std::cerr << "*** ERROR: GPIOD operation failed for RX \"" << rx_name 
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
