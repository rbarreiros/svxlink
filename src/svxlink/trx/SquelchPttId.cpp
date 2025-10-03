/**
@file	 SquelchPttId.cpp
@brief   A PTT ID squelch detector that validates DTMF user IDs
@author  Rui Barreiros / CR7BPM
@date	 2024-12-19

\verbatim
SvxLink - A Multi Purpose Voice Services System for Ham Radio Use
Copyright (C) 2003-2022 Tobias Blomberg / SM0SVX

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
#include <cstdlib>
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

#include "SquelchPttId.h"
#include "SvxSwDtmfDecoder.h"


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

SquelchPttId::SquelchPttId(void)
  : m_pttid_required(false), m_pttid_timeout_ms(5000), m_valid_user_detected(false)
{
} /* SquelchPttId::SquelchPttId */


SquelchPttId::~SquelchPttId(void)
{
  stopTimeoutTimer();
} /* SquelchPttId::~SquelchPttId */


bool SquelchPttId::initialize(Async::Config& cfg, const std::string& rx_name)
{
  // Load PTT ID configuration
  cfg.getValue(rx_name, "PTTID_REQUIRED", m_pttid_required);
  cfg.getValue(rx_name, "PTTID_TIMEOUT", m_pttid_timeout_ms);
  
  if (m_pttid_timeout_ms <= 0)
  {
    m_pttid_timeout_ms = 5000; // Default 5 seconds
  }

  // Load user and account configurations
  loadUserConfiguration(cfg);
  loadAccountConfiguration(cfg);

  // Create DTMF decoder
  m_dtmf_decoder = std::make_unique<SvxSwDtmfDecoder>(cfg, rx_name);
  if (!m_dtmf_decoder->initialize())
  {
    cerr << "*** ERROR: Failed to initialize DTMF decoder for PTT ID detector"
         << endl;
    return false;
  }

  // Connect DTMF decoder signals
  m_dtmf_decoder->digitActivated.connect(
      sigc::mem_fun(*this, &SquelchPttId::onDtmfDigitActivated));
  m_dtmf_decoder->digitDeactivated.connect(
      sigc::mem_fun(*this, &SquelchPttId::onDtmfDigitDeactivated));

  cout << rx_name << ": PTT ID squelch detector initialized. "
       << "Required=" << (m_pttid_required ? "yes" : "no")
       << ", Timeout=" << m_pttid_timeout_ms << "ms"
       << ", Users=" << m_users.size() << endl;

  return Squelch::initialize(cfg, rx_name);
} /* SquelchPttId::initialize */


void SquelchPttId::reset(void)
{
  stopTimeoutTimer();
  m_current_dtmf_id.clear();
  m_valid_user_detected = false;
  m_detected_username.clear();
  
  if (m_dtmf_decoder)
  {
    // DTMF decoder doesn't have a reset method, just clear our state
  }
  
  Squelch::reset();
} /* SquelchPttId::reset */


void SquelchPttId::restart(void)
{
  reset();
} /* SquelchPttId::restart */


/****************************************************************************
 *
 * Protected member functions
 *
 ****************************************************************************/

int SquelchPttId::processSamples(const float *samples, int count)
{
  if (m_dtmf_decoder)
  {
    return m_dtmf_decoder->writeSamples(samples, count);
  }
  return count;
} /* SquelchPttId::processSamples */


/****************************************************************************
 *
 * Private member functions
 *
 ****************************************************************************/

// Removed onDtmfDigitDetected - not needed since we use digitActivated and digitDeactivated


void SquelchPttId::onDtmfDigitActivated(char digit)
{
  // Start timeout timer when first digit is detected
  if (m_current_dtmf_id.empty())
  {
    startTimeoutTimer();
  }
  
  m_current_dtmf_id += digit;
  
  // Limit DTMF ID length to prevent memory issues
  if (m_current_dtmf_id.length() > 20)
  {
    m_current_dtmf_id = m_current_dtmf_id.substr(0, 20);
  }
} /* SquelchPttId::onDtmfDigitActivated */


void SquelchPttId::onDtmfDigitDeactivated(char digit, int duration)
{
  // Process the complete DTMF ID when we have a reasonable pause
  // For now, we'll process immediately when a digit is deactivated
  // In a real implementation, you might want to wait for a longer pause
  if (!m_current_dtmf_id.empty())
  {
    processDtmfId(m_current_dtmf_id);
    m_current_dtmf_id.clear();
  }
} /* SquelchPttId::onDtmfDigitDeactivated */


void SquelchPttId::onTimeout(Async::Timer *timer)
{
  // Timeout occurred - process whatever DTMF ID we have
  if (!m_current_dtmf_id.empty())
  {
    processDtmfId(m_current_dtmf_id);
    m_current_dtmf_id.clear();
  }
  stopTimeoutTimer();
} /* SquelchPttId::onTimeout */


void SquelchPttId::processDtmfId(const std::string& dtmf_id)
{
  cout << rxName() << ": Processing DTMF ID: " << dtmf_id << endl;
  
  // Update blocked users before checking
  updateBlockedUsers();
  
  // Look up the DTMF ID in our user map
  auto user_it = m_users.find(dtmf_id);
  if (user_it == m_users.end())
  {
    cout << rxName() << ": Unknown DTMF ID: " << dtmf_id << endl;
    if (m_pttid_required)
    {
      // Unknown user - don't open squelch
      setSignalDetected(false, "UNKNOWN_USER");
    }
    return;
  }
  
  const std::string& username = user_it->second;
  cout << rxName() << ": DTMF ID " << dtmf_id << " maps to user: " << username << endl;
  
  // Check if user is valid (active and not blocked)
  if (isUserValid(username))
  {
    m_valid_user_detected = true;
    m_detected_username = username;
    setSignalDetected(true, "USER:" + username);
    cout << rxName() << ": Valid user detected: " << username << endl;
  }
  else
  {
    cout << rxName() << ": User " << username << " is not valid (inactive or blocked)" << endl;
    if (m_pttid_required)
    {
      setSignalDetected(false, "INVALID_USER:" + username);
    }
  }
} /* SquelchPttId::processDtmfId */


bool SquelchPttId::isUserValid(const std::string& username)
{
  auto account_it = m_accounts.find(username);
  if (account_it == m_accounts.end())
  {
    // No account info - assume valid if user exists
    return true;
  }
  
  const UserAccount& account = account_it->second;
  
  // Check if user is active
  if (!account.active)
  {
    return false;
  }
  
  // Check if user is blocked
  auto now = std::chrono::steady_clock::now();
  if (now < account.blocked_until)
  {
    return false;
  }
  
  return true;
} /* SquelchPttId::isUserValid */


void SquelchPttId::updateBlockedUsers()
{
  auto now = std::chrono::steady_clock::now();
  
  for (auto& pair : m_accounts)
  {
    UserAccount& account = pair.second;
    if (account.blocked_minutes > 0 && now >= account.blocked_until)
    {
      // Blocking period has expired
      account.blocked_minutes = 0;
      cout << rxName() << ": User " << pair.first << " blocking period expired" << endl;
    }
  }
} /* SquelchPttId::updateBlockedUsers */


void SquelchPttId::loadUserConfiguration(Async::Config& cfg)
{
  m_users.clear();
  
  // Load PTTID_USERS section
  std::list<std::string> users = cfg.listSection("PTTID_USERS");
  for (const auto& user : users)
  {
    std::string dtmf_id = user;
    std::string username = cfg.getValue("PTTID_USERS", user);
    
    if (!dtmf_id.empty() && !username.empty())
    {
      m_users[dtmf_id] = username;
      cout << "PTT ID user: " << dtmf_id << " -> " << username << endl;
    }
  }
} /* SquelchPttId::loadUserConfiguration */


void SquelchPttId::loadAccountConfiguration(Async::Config& cfg)
{
  m_accounts.clear();
  
  // Load PTTID_ACCOUNTS section
  std::list<std::string> accounts = cfg.listSection("PTTID_ACCOUNTS");
  for (const auto& account : accounts)
  {
    std::string value = cfg.getValue("PTTID_ACCOUNTS", account);
    
    // Parse account configuration
    // Format: USERNAME_NAME, USERNAME_ACTIVE, USERNAME_BLOCKED
    size_t underscore_pos = account.find('_');
    if (underscore_pos != std::string::npos)
    {
      std::string username = account.substr(0, underscore_pos);
      std::string property = account.substr(underscore_pos + 1);
      
      // Initialize account if it doesn't exist
      if (m_accounts.find(username) == m_accounts.end())
      {
        m_accounts[username] = UserAccount();
      }
      
      UserAccount& user_account = m_accounts[username];
      
      if (property == "NAME")
      {
        user_account.name = value;
      }
      else if (property == "ACTIVE")
      {
        user_account.active = (value == "1" || value == "true" || value == "yes");
      }
      else if (property == "BLOCKED")
      {
        user_account.blocked_minutes = std::atoi(value.c_str());
        if (user_account.blocked_minutes > 0)
        {
          user_account.blocked_until = std::chrono::steady_clock::now() + 
              std::chrono::minutes(user_account.blocked_minutes);
        }
      }
    }
  }
  
  cout << "Loaded " << m_accounts.size() << " PTT ID accounts" << endl;
} /* SquelchPttId::loadAccountConfiguration */


void SquelchPttId::startTimeoutTimer()
{
  stopTimeoutTimer();
  m_timeout_timer = std::make_unique<Async::Timer>(m_pttid_timeout_ms);
  m_timeout_timer->expired.connect(
      sigc::mem_fun(*this, &SquelchPttId::onTimeout));
} /* SquelchPttId::startTimeoutTimer */


void SquelchPttId::stopTimeoutTimer()
{
  if (m_timeout_timer)
  {
    m_timeout_timer->setEnable(false);
    m_timeout_timer.reset();
  }
} /* SquelchPttId::stopTimeoutTimer */



/*
 * This file has not been truncated
 */
