/**
@file    Scheduler.cpp
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


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <ctime>
#include <sstream>
#include <iostream>
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

#include "Scheduler.h"


/****************************************************************************
 *
 * Namespaces to use
 *
 ****************************************************************************/

using namespace std;
using namespace Async;


/****************************************************************************
 *
 * ScheduleEntry implementation
 *
 ****************************************************************************/

bool ScheduleEntry::matches(const struct tm& t) const
{
  if (fired_year)
  {
    return false;
  }
  if ((year > 0) && ((t.tm_year + 1900) != year))
  {
    return false;
  }
  auto fieldMatch = [](const set<int>& s, int val) -> bool {
    return s.empty() || (s.find(val) != s.end());
  };
  return fieldMatch(minutes, t.tm_min)  &&
         fieldMatch(hours,   t.tm_hour) &&
         fieldMatch(doms,    t.tm_mday) &&
         fieldMatch(months,  t.tm_mon + 1) &&
         fieldMatch(dows,    t.tm_wday);
} /* ScheduleEntry::matches */


/****************************************************************************
 *
 * Scheduler implementation
 *
 ****************************************************************************/

Scheduler::Scheduler(void)
  : m_tick_timer(60000, Timer::TYPE_PERIODIC, false)
{
  m_tick_timer.expired.connect(
      sigc::mem_fun(*this, &Scheduler::onTick));
} /* Scheduler::Scheduler */


Scheduler::~Scheduler(void)
{
  m_tick_timer.setEnable(false);
} /* Scheduler::~Scheduler */


bool Scheduler::initialize(Async::Config& cfg, const string& section,
                           uint32_t default_tg)
{
  m_default_tg = default_tg;

  vector<string> schedule_names;
  cfg.getValue(section, "SCHEDULES", schedule_names);
  if (schedule_names.empty())
  {
    cout << "SvxPlayer: No schedules configured" << endl;
    return true;
  }

  for (const auto& name : schedule_names)
  {
    ScheduleEntry entry;
    entry.name = name;

    string files_str;
    if (cfg.getValue(name, "FILES", files_str) && !files_str.empty())
    {
      istringstream fss(files_str);
      string f;
      while (getline(fss, f, ','))
      {
        f.erase(0, f.find_first_not_of(" \t"));
        f.erase(f.find_last_not_of(" \t") + 1);
        if (!f.empty())
        {
          entry.files.push_back(f);
        }
      }
    }
    else
    {
      string single_file;
      if (!cfg.getValue(name, "FILE", single_file) || single_file.empty())
      {
        cerr << "*** ERROR: Schedule entry '" << name
             << "' must have FILE or FILES configured" << endl;
        return false;
      }
      entry.files.push_back(single_file);
    }

    if (entry.files.empty())
    {
      cerr << "*** ERROR: Schedule entry '" << name
           << "' has no valid files in FILES list" << endl;
      return false;
    }

    entry.tg = m_default_tg;
    cfg.getValue(name, "TG", entry.tg);

    entry.gap_s = 0;
    cfg.getValue(name, "GAP", entry.gap_s);

    string field;

    field = "*";
    cfg.getValue(name, "MINUTE", field);
    if (!parseField(field, 0, 59, entry.minutes))
    {
      cerr << "*** ERROR: Schedule entry '" << name
           << "' has invalid MINUTE field: " << field << endl;
      return false;
    }

    field = "*";
    cfg.getValue(name, "HOUR", field);
    if (!parseField(field, 0, 23, entry.hours))
    {
      cerr << "*** ERROR: Schedule entry '" << name
           << "' has invalid HOUR field: " << field << endl;
      return false;
    }

    field = "*";
    cfg.getValue(name, "DOM", field);
    if (!parseField(field, 1, 31, entry.doms))
    {
      cerr << "*** ERROR: Schedule entry '" << name
           << "' has invalid DOM field: " << field << endl;
      return false;
    }

    field = "*";
    cfg.getValue(name, "MONTH", field);
    if (!parseField(field, 1, 12, entry.months))
    {
      cerr << "*** ERROR: Schedule entry '" << name
           << "' has invalid MONTH field: " << field << endl;
      return false;
    }

    field = "*";
    cfg.getValue(name, "DOW", field);
    if (!parseField(field, 0, 6, entry.dows))
    {
      cerr << "*** ERROR: Schedule entry '" << name
           << "' has invalid DOW field: " << field << endl;
      return false;
    }

    int year_val = 0;
    string year_str = "*";
    cfg.getValue(name, "YEAR", year_str);
    if (year_str != "*" && !year_str.empty())
    {
      try
      {
        year_val = stoi(year_str);
        if (year_val < 2000 || year_val > 9999)
        {
          cerr << "*** ERROR: Schedule entry '" << name
               << "' has invalid YEAR: " << year_str << endl;
          return false;
        }
      }
      catch (const exception&)
      {
        cerr << "*** ERROR: Schedule entry '" << name
             << "' has non-numeric YEAR: " << year_str << endl;
        return false;
      }
    }
    entry.year = year_val;

    m_entries.push_back(entry);
    string files_log;
    for (size_t i = 0; i < entry.files.size(); ++i)
    {
      if (i > 0) files_log += ", ";
      files_log += entry.files[i];
    }
    cout << "SvxPlayer: Loaded schedule entry '" << name
         << "' -> [" << files_log << "]"
         << " on TG " << entry.tg
         << (entry.gap_s > 0 ? " gap=" + to_string(entry.gap_s) + "s" : "")
         << endl;
  }

  if (!m_entries.empty())
  {
    m_tick_timer.setEnable(true);

    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    cout << "SvxPlayer: Scheduler started, checking "
         << m_entries.size() << " entries every minute" << endl;

    onTick(nullptr);
  }

  return true;
} /* Scheduler::initialize */


void Scheduler::onTick(Async::Timer*)
{
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);

  for (auto& entry : m_entries)
  {
    if (entry.matches(t))
    {
      string files_log;
      for (size_t i = 0; i < entry.files.size(); ++i)
      {
        if (i > 0) files_log += ", ";
        files_log += entry.files[i];
      }
      cout << "SvxPlayer: Schedule '" << entry.name
           << "' fired: [" << files_log << "]"
           << " on TG " << entry.tg
           << (entry.gap_s > 0 ? " gap=" + to_string(entry.gap_s) + "s" : "")
           << endl;
      if ((entry.year > 0) && ((t.tm_year + 1900) == entry.year))
      {
        entry.fired_year = true;
      }
      playFiles(entry.files, entry.tg, entry.gap_s * 1000);
    }
  }
} /* Scheduler::onTick */


bool Scheduler::parseField(const string& s, int lo, int hi, set<int>& out)
{
  out.clear();
  if (s == "*" || s.empty())
  {
    return true;
  }

  istringstream ss(s);
  string token;
  while (getline(ss, token, ','))
  {
    string::size_type dash = token.find('-');
    if (dash != string::npos)
    {
      string lo_str = token.substr(0, dash);
      string hi_str = token.substr(dash + 1);
      try
      {
        int a = stoi(lo_str);
        int b = stoi(hi_str);
        if (a < lo || b > hi || a > b)
        {
          return false;
        }
        for (int i = a; i <= b; ++i)
        {
          out.insert(i);
        }
      }
      catch (const exception&)
      {
        return false;
      }
    }
    else
    {
      try
      {
        int v = stoi(token);
        if (v < lo || v > hi)
        {
          return false;
        }
        out.insert(v);
      }
      catch (const exception&)
      {
        return false;
      }
    }
  }

  return true;
} /* Scheduler::parseField */


/*
 * This file has not been truncated
 */
