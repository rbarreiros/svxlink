/**
@file    Scheduler.h
@brief   Cron-like scheduler for svxplayer
@author  Rui Barreiros <rbarreiros@gmail.com>
@date    2026-02-27

\verbatim
SvxLink - A Multi Purpose Voice Services System for Ham Radio Use
Copyright (C) 2003-2026 Tobias Blomberg / SM0SVX

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

#ifndef SCHEDULER_INCLUDED
#define SCHEDULER_INCLUDED


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <string>
#include <vector>
#include <set>
#include <cstdint>

#include <sigc++/sigc++.h>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncConfig.h>
#include <AsyncTimer.h>


/****************************************************************************
 *
 * Forward declarations
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Class definitions
 *
 ****************************************************************************/

/**
@brief  A single schedule entry loaded from config
@author Rodrigo Barreiros
@date   2026-02-27

Holds the time matching fields (cron-style) and the file/TG to play.
Each field stores a set of matching values; an empty set means "wildcard" (*).
*/
struct ScheduleEntry
{
  std::string              name;
  std::vector<std::string> files;
  uint32_t                 tg          = 0;
  uint32_t                 gap_s       = 0;
  std::set<int>            minutes;
  std::set<int>            hours;
  std::set<int>            doms;
  std::set<int>            months;
  std::set<int>            dows;
  int                      year        = 0;
  bool                     fired_year  = false;

  /**
   * @brief  Return true if the entry matches the given broken-down time
   * @param  tm_in  A pointer to a struct tm with the current time
   */
  bool matches(const struct tm& t) const;
};


/**
@brief  Cron-like scheduler for svxplayer
@author Rodrigo Barreiros
@date   2026-02-27

Loads schedule entries from the configuration file and fires the
playFile signal when a matching minute occurs.  Uses Async::Timer
so it integrates with the Async event loop.
*/
class Scheduler : public sigc::trackable
{
  public:
    /**
     * @brief  Emitted when a scheduled entry fires
     * @param  files      Ordered list of audio file paths to play
     * @param  tg         Talk group (0 = use DEFAULT_TG)
     * @param  gap_ms     Milliseconds of silence (PTT released) between files
     */
    sigc::signal<void(const std::vector<std::string>&, uint32_t, uint32_t)>
        playFiles;

    /**
     * @brief  Constructor
     */
    Scheduler(void);

    /**
     * @brief  Destructor
     */
    ~Scheduler(void);

    /**
     * @brief  Load schedule entries from config
     * @param  cfg          Configuration object
     * @param  section      Main config section (e.g. "SvxPlayer")
     * @param  default_tg   Default TG to use when entry has no TG set
     * @return true on success
     */
    bool initialize(Async::Config& cfg, const std::string& section,
                    uint32_t default_tg);

    /**
     * @brief  Return the number of loaded schedule entries
     */
    size_t entryCount(void) const { return m_entries.size(); }

  private:
    Async::Timer              m_tick_timer;
    std::vector<ScheduleEntry> m_entries;
    uint32_t                  m_default_tg = 0;

    void onTick(Async::Timer*);

    static bool parseField(const std::string& s, int lo, int hi,
                           std::set<int>& out);
};


#endif /* SCHEDULER_INCLUDED */

/*
 * This file has not been truncated
 */
