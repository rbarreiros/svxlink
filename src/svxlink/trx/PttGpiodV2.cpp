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
{
} /* PttGpiod::PttGpiod */


PttGpiod::~PttGpiod(void)
{
  m_line.reset();
  m_chip.reset();
} /* PttGpiod::~PttGpiod */


bool PttGpiod::initialize(Async::Config &cfg, const std::string name)
{
  std::string chip("gpiochip0");
  cfg.getValue(name, "PTT_GPIOD_CHIP", chip);

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

    // Configure the line for output
    gpiod::line_request_config req_cfg;
    req_cfg.consumer = "SvxLink";
    req_cfg.request_type = gpiod::line_request::DIRECTION_OUTPUT;
    req_cfg.flags = active_low ? gpiod::line_request::FLAG_ACTIVE_LOW : 0;

    // Request the line
    m_line->request(req_cfg, active_low ? 1 : 0);
  }
  catch (const gpiod::exception& e)
  {
    std::cerr << "*** ERROR: GPIOD operation failed for TX " << name 
              << ": " << e.what() << std::endl;
    return false;
  }
  catch (const std::exception& e)
  {
    std::cerr << "*** ERROR: Unexpected error for TX " << name 
              << ": " << e.what() << std::endl;
    return false;
  }

  return true;
} /* PttGpiod::initialize */


bool PttGpiod::setTxOn(bool tx_on)
{
  try
  {
    m_line->set_value(tx_on ? 1 : 0);
  }
  catch (const gpiod::exception& e)
  {
    std::cerr << "*** WARNING: PttGpiod::setTxOn: "
                 "gpiod_line_set_value failed: "
              << e.what() << std::endl;
    return false;
  }
  catch (const std::exception& e)
  {
    std::cerr << "*** WARNING: PttGpiod::setTxOn: "
                 "Unexpected error: "
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
