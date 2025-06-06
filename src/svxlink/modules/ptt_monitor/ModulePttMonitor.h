/****************************************************************************
 *
 * ModulePttMonitor.h -- PTT Activity Monitor Module for SvxLink
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

#ifndef MODULE_PTT_MONITOR_H
#define MODULE_PTT_MONITOR_H

/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <string>
#include <vector>
#include <ctime>

/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <Module.h>
#include <AsyncTimer.h>

/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

 #include "version/SVXLINK.h"

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
 * @brief   PTT activity monitoring module for SvxLink
 * @author  Your Name / Your Callsign
 * @date    2025
 *
 * This module monitors PTT activations and can send warning messages and
 * eventually set the system to listen-only mode if excessive activity is
 * detected within configurable timeframes.
 */
class ModulePttMonitor : public Module
{
  public:
    /**
     * @brief   Constructor
     * @param   logic - Pointer to the logic core
     * @param   name - Module name
     */
    ModulePttMonitor(void *dl_handle, Logic *logic, const std::string& name);
    virtual ~ModulePttMonitor(void);
    const char *compiledForVersion(void) const { return SVXLINK_APP_VERSION; }

  protected:
    virtual void resumeOutput(void);
    virtual void allSamplesFlushed(void);
    virtual int writeSamples(const float *samples, int count);
    virtual void flushSamples(void);

  private:
    bool initialize(void);
    void activateInit(void);
    void deactivateCleanup(void);
    bool dtmfDigitReceived(char digit, int duration);
    void dtmfCmdReceived(const std::string& cmd);
    void squelchOpen(bool is_open);
    
    struct PttEvent 
    {
      time_t timestamp;
      bool is_open;
    };
    
    // Configuration variables
    unsigned int    max_activations;        // X - max PTT activations in timeframe
    unsigned int    warning_messages;       // Y - max warning messages before listen-only
    unsigned int    monitoring_timeframe;   // Timeframe in seconds for monitoring
    unsigned int    reset_timeout;          // Timeout in seconds for counter reset
    std::string     warning_message_id;     // Message ID to play as warning
    
    // Runtime state
    std::vector<PttEvent> ptt_events;      // Recent PTT events
    unsigned int    warning_count;          // Number of warnings sent
    bool            listen_only_active;     // Current listen-only state
    bool            monitoring_active;      // Module monitoring state
    time_t          last_activity_time;     // Last PTT activity timestamp
    
    // Timers
    Async::Timer    *reset_timer;           // Timer for resetting counters
    Async::Timer    *cleanup_timer;         // Timer for periodic cleanup
    
    /**
     * @brief   Check if PTT activity exceeds thresholds
     */
    void checkPttActivity(void);
    
    /**
     * @brief   Send warning message using internal API
     */
    void sendWarningMessage(void);
    
    /**
     * @brief   Set system to listen-only mode
     */
    void activateListenOnlyMode(void);
    
    /**
     * @brief   Reset monitoring counters and state
     */
    void resetCounters(void);
    
    /**
     * @brief   Clean up old PTT events outside timeframe
     */
    void cleanupOldEvents(void);
    
    /**
     * @brief   Timer callback for reset timeout
     */
    void onResetTimeout(Async::Timer *timer);
    
    /**
     * @brief   Timer callback for periodic cleanup
     */
    void onCleanupTimeout(Async::Timer *timer);
    
};  /* class ModulePttMonitor */

#endif /* MODULE_PTT_MONITOR_H */

/****************************************************************************
 *
 * This file has not been truncated
 *
 ****************************************************************************/
