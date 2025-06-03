/****************************************************************************
 *
 * ModulePttMonitor.cpp -- PTT Activity Monitor Module for SvxLink
 *
 * This module monitors PTT activations and can send warning messages and
 * eventually set the system to listen-only mode if excessive activity is
 * detected within configurable timeframes.
 *
 * Copyright (C) 2025 Your Name / Your Callsign
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 ****************************************************************************/

/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <iostream>
#include <algorithm>
#include <sstream>
#include <ctime>

/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncConfig.h>
#include <AsyncTimer.h>

/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "version/MODULE_PTT_MONITOR.h"
#include "ModulePttMonitor.h"

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
 * Pure C functions
 *
 ****************************************************************************/

/****************************************************************************
 *
 * Public member functions
 *
 ****************************************************************************/

ModulePttMonitor::ModulePttMonitor(void *dl_handle, Logic *logic, const std::string& name)
  : Module(dl_handle, logic, name), max_activations(5), warning_messages(3),
    monitoring_timeframe(300), reset_timeout(600), warning_message_id(""),
    warning_count(0), listen_only_active(false), monitoring_active(false),
    last_activity_time(0), reset_timer(0), cleanup_timer(0)
{
  cout << "ModulePttMonitor: Module " << name << " created" << endl;
} /* ModulePttMonitor::ModulePttMonitor */

ModulePttMonitor::~ModulePttMonitor(void)
{
  delete reset_timer;
  delete cleanup_timer;
  cout << "ModulePttMonitor: Module " << name() << " destroyed" << endl;
} /* ModulePttMonitor::~ModulePttMonitor */

bool ModulePttMonitor::initialize(void)
{
  if (!Module::initialize())
  {
    return false;
  }

  // Read configuration variables
  string value;
  
  if (cfg().getValue(cfgName(), "MAX_ACTIVATIONS", value))
  {
    max_activations = atoi(value.c_str());
  }
  
  if (cfg().getValue(cfgName(), "WARNING_MESSAGES", value))
  {
    warning_messages = atoi(value.c_str());
  }
  
  if (cfg().getValue(cfgName(), "MONITORING_TIMEFRAME", value))
  {
    monitoring_timeframe = atoi(value.c_str());
  }
  
  if (cfg().getValue(cfgName(), "RESET_TIMEOUT", value))
  {
    reset_timeout = atoi(value.c_str());
  }
  
  if (cfg().getValue(cfgName(), "WARNING_MESSAGE_ID", warning_message_id))
  {
    // Successfully read warning message ID
  }

  // Validate configuration
  if (max_activations == 0)
  {
    cerr << "*** ERROR: " << cfgName() << "/MAX_ACTIVATIONS must be > 0" << endl;
    return false;
  }
  
  if (warning_messages == 0)
  {
    cerr << "*** ERROR: " << cfgName() << "/WARNING_MESSAGES must be > 0" << endl;
    return false;
  }
  
  if (monitoring_timeframe == 0)
  {
    cerr << "*** ERROR: " << cfgName() << "/MONITORING_TIMEFRAME must be > 0" << endl;
    return false;
  }
  
  if (reset_timeout == 0)
  {
    cerr << "*** ERROR: " << cfgName() << "/RESET_TIMEOUT must be > 0" << endl;
    return false;
  }

  // Create timers
  reset_timer = new Timer(reset_timeout * 1000);
  reset_timer->expired.connect(mem_fun(*this, &ModulePttMonitor::onResetTimeout));
  
  cleanup_timer = new Timer(60000); // Run cleanup every minute
  cleanup_timer->setEnable(true);
  cleanup_timer->expired.connect(mem_fun(*this, &ModulePttMonitor::onCleanupTimeout));

  cout << "ModulePttMonitor: Configuration - MAX_ACTIVATIONS=" << max_activations 
       << ", WARNING_MESSAGES=" << warning_messages 
       << ", MONITORING_TIMEFRAME=" << monitoring_timeframe << "s"
       << ", RESET_TIMEOUT=" << reset_timeout << "s" << endl;

  return true;
} /* ModulePttMonitor::initialize */

/****************************************************************************
 *
 * Protected member functions
 *
 ****************************************************************************/

void ModulePttMonitor::activateInit(void)
{
  cout << "ModulePttMonitor: Module activated" << endl;
  monitoring_active = true;
  
  // Start monitoring if not already in listen-only mode
  if (!listen_only_active)
  {
    cout << "ModulePttMonitor: PTT monitoring started" << endl;
  }
} /* ModulePttMonitor::activateInit */

void ModulePttMonitor::deactivateCleanup(void)
{
  cout << "ModulePttMonitor: Module deactivated" << endl;
  monitoring_active = false;
} /* ModulePttMonitor::deactivateCleanup */

bool ModulePttMonitor::dtmfDigitReceived(char digit, int duration)
{
  // This module doesn't handle individual DTMF digits
  // All functionality is triggered by squelch events
  return false;
} /* ModulePttMonitor::dtmfDigitReceived */

void ModulePttMonitor::dtmfCmdReceived(const std::string& cmd)
{
  cout << "ModulePttMonitor: DTMF command received: " << cmd << endl;
  
  // Handle administrative commands if needed
  if (cmd == "0")
  {
    // Help command - could play help about PTT monitor
    processEvent("play_help");
  }
  else if (cmd == "99")
  {
    // Reset counters command (admin function)
    resetCounters();
    processEvent("ptt_monitor_reset");
  }
  else if (cmd == "98")
  {
    // Status command - report current state
    stringstream ss;
    ss << "ptt_monitor_status " << ptt_events.size() << " " << warning_count;
    processEvent(ss.str());
  }
  else if (cmd == "")
  {
    // Empty command - deactivate module
    //deactivateModule();
    deactivateMe();
  }
  else
  {
    // Unknown command
    processEvent("unknown_command " + cmd);
  }
} /* ModulePttMonitor::dtmfCmdReceived */

void ModulePttMonitor::squelchOpen(bool is_open)
{
  if (!monitoring_active || listen_only_active)
  {
    return;
  }

  time_t now = time(NULL);
  
  if (is_open)
  {
    // PTT activated - record the event
    PttEvent event;
    event.timestamp = now;
    event.is_open = true;
    ptt_events.push_back(event);
    
    last_activity_time = now;
    
    cout << "ModulePttMonitor: PTT activation recorded (" 
         << ptt_events.size() << " events in history)" << endl;
    
    // Check if we need to take action
    checkPttActivity();
    
    // Reset/restart the reset timer
    reset_timer->setEnable(false);
    reset_timer->setEnable(true);
  }
  else
  {
    // PTT released - record the event
    PttEvent event;
    event.timestamp = now;
    event.is_open = false;
    ptt_events.push_back(event);
  }
} /* ModulePttMonitor::squelchOpen */

/****************************************************************************
 *
 * Private member functions
 *
 ****************************************************************************/

void ModulePttMonitor::checkPttActivity(void)
{
  time_t now = time(NULL);
  time_t threshold_time = now - monitoring_timeframe;
  
  // Count PTT activations (squelch open events) within timeframe
  unsigned int activations_in_timeframe = 0;
  
  for (vector<PttEvent>::const_iterator it = ptt_events.begin(); 
       it != ptt_events.end(); ++it)
  {
    if (it->timestamp >= threshold_time && it->is_open)
    {
      activations_in_timeframe++;
    }
  }
  
  cout << "ModulePttMonitor: " << activations_in_timeframe 
       << " PTT activations in last " << monitoring_timeframe << " seconds" << endl;
  
  if (activations_in_timeframe >= max_activations)
  {
    cout << "ModulePttMonitor: Excessive PTT activity detected!" << endl;
    
    if (warning_count < warning_messages)
    {
      // Send warning message
      sendWarningMessage();
      warning_count++;
      
      cout << "ModulePttMonitor: Warning message sent (" 
           << warning_count << "/" << warning_messages << ")" << endl;
    }
    else
    {
      // Activate listen-only mode
      activateListenOnlyMode();
    }
  }
} /* ModulePttMonitor::checkPttActivity */

void ModulePttMonitor::sendWarningMessage(void)
{
  if (warning_message_id.empty())
  {
    // Use default warning message
    processEvent("ptt_monitor_warning");
  }
  else
  {
    // Use configured message ID
    stringstream ss;
    ss << "play_msg " << warning_message_id;
    processEvent(ss.str());
  }
} /* ModulePttMonitor::sendWarningMessage */

void ModulePttMonitor::activateListenOnlyMode(void)
{
  listen_only_active = true;
  
  cout << "ModulePttMonitor: Activating listen-only mode due to excessive PTT activity" << endl;
  
  // Send notification that listen-only mode is activated
  processEvent("ptt_monitor_listen_only_activated");
  
  // Here you would integrate with the Logic core to actually disable transmission
  // This depends on the specific Logic implementation being used
  // For SimplexLogic or RepeaterLogic, you might need to:
  // 1. Get reference to the Logic object
  // 2. Call a method to disable transmission
  // 3. Or use the internal event system
  
  // Example using internal event system:
  processEvent("set_listen_only 1");
} /* ModulePttMonitor::activateListenOnlyMode */

void ModulePttMonitor::resetCounters(void)
{
  ptt_events.clear();
  warning_count = 0;
  listen_only_active = false;
  last_activity_time = 0;
  
  cout << "ModulePttMonitor: Counters reset, monitoring resumed" << endl;
  
  // Reset listen-only mode
  processEvent("set_listen_only 0");
  processEvent("ptt_monitor_reset_complete");
} /* ModulePttMonitor::resetCounters */

void ModulePttMonitor::cleanupOldEvents(void)
{
  time_t now = time(NULL);
  time_t threshold_time = now - (monitoring_timeframe * 2); // Keep twice the timeframe
  
  // Remove events older than threshold
  vector<PttEvent>::iterator it = ptt_events.begin();
  while (it != ptt_events.end())
  {
    if (it->timestamp < threshold_time)
    {
      it = ptt_events.erase(it);
    }
    else
    {
      ++it;
    }
  }
} /* ModulePttMonitor::cleanupOldEvents */

void ModulePttMonitor::onResetTimeout(Timer *timer)
{
  time_t now = time(NULL);
  
  if (last_activity_time > 0 && (now - last_activity_time) >= reset_timeout)
  {
    cout << "ModulePttMonitor: No PTT activity for " << reset_timeout 
         << " seconds, resetting counters" << endl;
    resetCounters();
  }
} /* ModulePttMonitor::onResetTimeout */

void ModulePttMonitor::onCleanupTimeout(Timer *timer)
{
  cleanupOldEvents();
  
  // Restart the cleanup timer
  cleanup_timer->setEnable(true);
} /* ModulePttMonitor::onCleanupTimeout */

/****************************************************************************
 *
 * This file has not been truncated
 *
 ****************************************************************************/
