#include "LogicScheduler.h"
#include "Logic.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>

using namespace std;

LogicScheduler::LogicScheduler(Logic *logic)
  : m_logic(logic), enabled(false), debug_enabled(false)
{
  macro_prefix = "D";
  m_logic->cfg().getValue("GLOBAL", "MACRO_PREFIX", macro_prefix);
}

LogicScheduler::~LogicScheduler(void)
{
}

bool LogicScheduler::initialize(void)
{
  // Check if scheduler is enabled
  string enabled_str;
  if (!m_logic->cfg().getValue("SCHEDULE", "ENABLED", enabled_str) || enabled_str != "1") {
    if (debug_enabled) {
      cout << m_logic->name() << ": Scheduler disabled" << endl;
    }
    return true; // Not an error, just disabled
  }
  
  enabled = true;
  
  // Load debug setting
  string debug_str;
  if (m_logic->cfg().getValue("SCHEDULE", "DEBUG", debug_str)) {
    debug_enabled = (debug_str == "1");
  }
  
  loadConfiguration();
  
  if (debug_enabled) {
    cout << m_logic->name() << ": Scheduler initialized with " << messages.size() << " messages" << endl;
  }
  
  return true;
}

void LogicScheduler::loadConfiguration(void)
{
  messages.clear();
  time_intervals.clear();
  
  if (!enabled) {
    return;
  }
  
  // Get list of messages
  vector<string> message_names;
  string messages_str;
  if (m_logic->cfg().getValue("SCHEDULE", "MESSAGES", messages_str)) {
    // Parse comma-separated list
    stringstream ss(messages_str);
    string item;
    while (getline(ss, item, ',')) {
      // Trim whitespace
      item.erase(0, item.find_first_not_of(" \t"));
      item.erase(item.find_last_not_of(" \t") + 1);
      if (!item.empty()) {
        message_names.push_back(item);
      }
    }
  }
  
  // Also check for individual message definitions
  vector<string> individual_keys = {"WEATHER", "NEWS", "ANNOUNCEMENT", "ID", "WEATHER_ALERT"};
  for (const auto& key : individual_keys) {
    string msg_name;
    if (m_logic->cfg().getValue("SCHEDULE", key, msg_name) && !msg_name.empty()) {
      message_names.push_back(msg_name);
    }
  }
  
  // Load each message configuration
  for (const auto& msg_name : message_names) {
    ScheduledMessage msg;
    msg.name = msg_name;
    
    // Get message configuration
    string days_str, times_str, file_str, macro_str, command_str, enabled_str, disabled_logics_str;
    m_logic->cfg().getValue(msg_name, "DAYS", days_str);
    m_logic->cfg().getValue(msg_name, "TIME", times_str);
    m_logic->cfg().getValue(msg_name, "FILE", file_str);
    m_logic->cfg().getValue(msg_name, "MACRO", macro_str);
    m_logic->cfg().getValue(msg_name, "COMMAND", command_str);
    m_logic->cfg().getValue(msg_name, "ENABLED", enabled_str);
    m_logic->cfg().getValue(msg_name, "DISABLE_LOGIC", disabled_logics_str);
    
    if (enabled_str == "0") {
      continue; // Skip disabled messages
    }
    
    if (times_str.empty()) {
      cout << "Warning: No TIME specified for message " << msg_name << endl;
      continue;
    }
    
    // Parse days, times, and disabled logics
    parseDays(days_str, msg.days);
    parseTimes(times_str, msg.times);
    parseDisabledLogics(disabled_logics_str, msg.disabled_logics);
    
    if (msg.days.empty() || msg.times.empty()) {
      cout << "Warning: Invalid configuration for message " << msg_name << endl;
      continue;
    }
    
    msg.file = file_str;
    if (!macro_str.empty()) {
      msg.macro = atoi(macro_str.c_str());
    }
    msg.command = command_str;
    
    messages[msg_name] = msg;
    
    if (debug_enabled) {
      cout << "Loaded message: " << msg_name << " - Days: ";
      for (int day : msg.days) cout << day << " ";
      cout << "Times: ";
      for (const string& time : msg.times) cout << time << " ";
      if (!msg.file.empty()) cout << "File=" << msg.file << " ";
      if (msg.macro >= 0) cout << "Macro=" << msg.macro << " ";
      if (!msg.command.empty()) cout << "Command=" << msg.command << " ";
      if (!msg.disabled_logics.empty()) {
        cout << "Disabled in: ";
        for (const string& logic : msg.disabled_logics) cout << logic << " ";
      }
      cout << endl;
    }
  }
}

void LogicScheduler::parseDays(const string& days_str, set<int>& days)
{
  days.clear();
  
  if (days_str.empty()) {
    return;
  }
  
  string upper_days = days_str;
  transform(upper_days.begin(), upper_days.end(), upper_days.begin(), ::toupper);
  
  if (upper_days == "ALL") {
    for (int i = 1; i <= 7; i++) {
      days.insert(i);
    }
    return;
  }
  
  if (upper_days == "WEEKDAYS") {
    for (int i = 1; i <= 5; i++) {
      days.insert(i);
    }
    return;
  }
  
  if (upper_days == "WEEKENDS") {
    days.insert(6); // Saturday
    days.insert(7); // Sunday
    return;
  }
  
  // Parse comma-separated list
  stringstream ss(days_str);
  string item;
  while (getline(ss, item, ',')) {
    // Trim whitespace
    item.erase(0, item.find_first_not_of(" \t"));
    item.erase(item.find_last_not_of(" \t") + 1);
    
    if (item.empty()) continue;
    
    // Check if it's a number (1-7)
    if (item.length() == 1 && item[0] >= '1' && item[0] <= '7') {
      days.insert(item[0] - '0');
    } else {
      // Check for day names
      transform(item.begin(), item.end(), item.begin(), ::toupper);
      if (item == "MON" || item == "MONDAY") days.insert(1);
      else if (item == "TUE" || item == "TUESDAY") days.insert(2);
      else if (item == "WED" || item == "WEDNESDAY") days.insert(3);
      else if (item == "THU" || item == "THURSDAY") days.insert(4);
      else if (item == "FRI" || item == "FRIDAY") days.insert(5);
      else if (item == "SAT" || item == "SATURDAY") days.insert(6);
      else if (item == "SUN" || item == "SUNDAY") days.insert(7);
    }
  }
}

void LogicScheduler::parseTimes(const string& times_str, set<string>& times)
{
  times.clear();
  
  if (times_str.empty()) {
    return;
  }
  
  // Parse comma-separated list
  stringstream ss(times_str);
  string item;
  while (getline(ss, item, ',')) {
    // Trim whitespace
    item.erase(0, item.find_first_not_of(" \t"));
    item.erase(item.find_last_not_of(" \t") + 1);
    
    if (item.empty()) continue;
    
    TimeInterval interval;
    parseTimeInterval(item, interval);
    
    if (interval.is_interval) {
      // Generate times for the interval
      if (interval.hour >= 0) {
        // Specific hour interval: HH:*/MM
        for (int min = 0; min < 60; min += interval.interval_minutes) {
          char time_str[8];
          snprintf(time_str, sizeof(time_str), "%02d:%02d", interval.hour, min);
          times.insert(time_str);
        }
      } else {
        // Every MM minutes: */MM
        for (int hour = 0; hour < 24; hour++) {
          for (int min = 0; min < 60; min += interval.interval_minutes) {
            char time_str[8];
            snprintf(time_str, sizeof(time_str), "%02d:%02d", hour, min);
            times.insert(time_str);
          }
        }
      }
    } else {
      // Specific time: HH:MM
      char time_str[8];
      snprintf(time_str, sizeof(time_str), "%02d:%02d", interval.hour, interval.minute);
      times.insert(time_str);
    }
  }
}

void LogicScheduler::parseDisabledLogics(const string& disabled_logics_str, set<string>& disabled_logics)
{
  disabled_logics.clear();
  
  if (disabled_logics_str.empty()) {
    return;
  }
  
  // Parse comma-separated list
  stringstream ss(disabled_logics_str);
  string item;
  while (getline(ss, item, ',')) {
    // Trim whitespace
    item.erase(0, item.find_first_not_of(" \t"));
    item.erase(item.find_last_not_of(" \t") + 1);
    
    if (!item.empty()) {
      disabled_logics.insert(item);
    }
  }
}

bool LogicScheduler::shouldPlayInCurrentLogic(const ScheduledMessage& msg)
{
  // If no disabled logics specified, play in all logics
  if (msg.disabled_logics.empty()) {
    return true;
  }
  
  // Check if current logic is in the disabled list
  string current_logic = m_logic->name();
  return msg.disabled_logics.find(current_logic) == msg.disabled_logics.end();
}

void LogicScheduler::parseTimeInterval(const string& time_str, TimeInterval& interval)
{
  // Check for interval format: */MM, */HH, or HH:*/MM
  size_t star_pos = time_str.find('*');
  if (star_pos != string::npos) {
    interval.is_interval = true;
    
    if (star_pos == 0) {
      // Format: */MM or */HH
      string interval_str = time_str.substr(2); // Skip "*/"
      int interval_value = atoi(interval_str.c_str());
      
      // Determine if it's minutes or hours based on value
      if (interval_value <= 60) {
        // Minutes
        interval.hour = -1; // All hours
        interval.interval_minutes = interval_value;
      } else {
        // Hours
        interval.hour = -1; // All hours
        interval.interval_minutes = interval_value * 60; // Convert to minutes
      }
    } else {
      // Format: HH:*/MM
      string hour_str = time_str.substr(0, star_pos - 1);
      interval.hour = atoi(hour_str.c_str());
      string minutes_str = time_str.substr(star_pos + 2); // Skip "*/"
      interval.interval_minutes = atoi(minutes_str.c_str());
    }
  } else {
    // Format: HH:MM
    interval.is_interval = false;
    size_t colon_pos = time_str.find(':');
    if (colon_pos != string::npos) {
      string hour_str = time_str.substr(0, colon_pos);
      string minute_str = time_str.substr(colon_pos + 1);
      interval.hour = atoi(hour_str.c_str());
      interval.minute = atoi(minute_str.c_str());
    }
  }
}

void LogicScheduler::checkScheduledMessages(void)
{
  if (!enabled) {
    return;
  }
  
  string current_time = getCurrentTime();
  int current_day = getCurrentDay();
  
  if (debug_enabled) {
    cout << m_logic->name() << ": Checking scheduled messages at " << current_time 
         << " (day " << current_day << ")" << endl;
  }
  
  for (const auto& msg_pair : messages) {
    const ScheduledMessage& msg = msg_pair.second;
    
    if (!msg.enabled) continue;
    
    // Check if this message should play in the current logic
    if (!shouldPlayInCurrentLogic(msg)) {
      if (debug_enabled) {
        cout << m_logic->name() << ": Skipping message " << msg.name 
             << " - disabled for this logic" << endl;
      }
      continue;
    }
    
    if (isDayMatch(current_day, msg.days) && isTimeMatch(current_time, msg.times)) {
      executeMessage(msg);
    }
  }
}

void LogicScheduler::executeMessage(const ScheduledMessage& msg)
{
  if (debug_enabled) {
    cout << m_logic->name() << ": Executing scheduled message: " << msg.name << endl;
  }
  
  if (!msg.file.empty()) {
    playFile(msg.file);
  }
  
  if (msg.macro >= 0) {
    executeMacro(msg.macro);
  }
  
  if (!msg.command.empty()) {
    executeCommand(msg.command);
  }
  
  if (msg.file.empty() && msg.macro < 0 && msg.command.empty()) {
    // Play default message
    m_logic->processEvent("playMsg scheduled_message");
    m_logic->processEvent("spellWord " + msg.name);
  }
}

void LogicScheduler::playFile(const string& filename)
{
  struct stat st;
  if (stat(filename.c_str(), &st) == 0) {
    m_logic->playFile(filename);
    if (debug_enabled) {
      cout << m_logic->name() << ": Played file: " << filename << endl;
    }
  } else {
    cout << "Warning: File not found: " << filename << endl;
  }
}

void LogicScheduler::executeMacro(int macro_num)
{
  if (debug_enabled) {
    cout << m_logic->name() << ": Executing macro: " << macro_num << endl;
  }
  
  // Execute the macro by sending the macro command to the logic
  string macro_cmd = macro_prefix + to_string(macro_num);
  m_logic->processMacroCmd(macro_cmd);
}


void LogicScheduler::executeCommand(const string& command)
{
  if (debug_enabled) {
    cout << m_logic->name() << ": Executing command: " << command << endl;
  }
  
  // Expand placeholders in the command
  string expanded_command = expandCommandPlaceholders(command);
  
  if (debug_enabled) {
    cout << m_logic->name() << ": Expanded command: " << expanded_command << endl;
  }
  
  // Execute the command in the background
  int result = system(expanded_command.c_str());
  
  if (debug_enabled) {
    if (result == 0) {
      cout << m_logic->name() << ": Command executed successfully" << endl;
    } else {
      cout << m_logic->name() << ": Command failed with exit code " << result << endl;
    }
  }
}

string LogicScheduler::expandCommandPlaceholders(const string& command)
{
  string result = command;
  
  // Get current time information
  time_t now = time(0);
  struct tm *tm_now = localtime(&now);
  
  // Replace placeholders
  // %LOGIC% - Logic name
  string logic_name = m_logic->name();
  size_t pos = 0;
  while ((pos = result.find("%LOGIC%", pos)) != string::npos) {
    result.replace(pos, 7, logic_name);
    pos += logic_name.length();
  }
  
  // %DATE% - Date only (YYYY-MM-DD)
  char date_str[16];
  strftime(date_str, sizeof(date_str), "%Y-%m-%d", tm_now);
  pos = 0;
  while ((pos = result.find("%DATE%", pos)) != string::npos) {
    result.replace(pos, 6, date_str);
    pos += strlen(date_str);
  }
  
  // %TIME% - Time only (HH:MM:SS)
  char time_str[16];
  strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_now);
  pos = 0;
  while ((pos = result.find("%TIME%", pos)) != string::npos) {
    result.replace(pos, 6, time_str);
    pos += strlen(time_str);
  }
  
  // %DATETIME% - Date and time (YYYY-MM-DD HH:MM:SS)
  char datetime_str[32];
  strftime(datetime_str, sizeof(datetime_str), "%Y-%m-%d %H:%M:%S", tm_now);
  pos = 0;
  while ((pos = result.find("%DATETIME%", pos)) != string::npos) {
    result.replace(pos, 10, datetime_str);
    pos += strlen(datetime_str);
  }
  
  return result;
}

string LogicScheduler::getCurrentTime(void)
{
  time_t now = time(0);
  struct tm *tm_now = localtime(&now);
  char time_str[6];
  snprintf(time_str, sizeof(time_str), "%02d:%02d", tm_now->tm_hour, tm_now->tm_min);
  return string(time_str);
}

int LogicScheduler::getCurrentDay(void)
{
  time_t now = time(0);
  struct tm *tm_now = localtime(&now);
  int day = tm_now->tm_wday;
  return (day == 0) ? 7 : day; // Convert Sunday from 0 to 7
}

bool LogicScheduler::isTimeMatch(const string& current_time, const set<string>& scheduled_times)
{
  return scheduled_times.find(current_time) != scheduled_times.end();
}

bool LogicScheduler::isDayMatch(int current_day, const set<int>& scheduled_days)
{
  return scheduled_days.find(current_day) != scheduled_days.end();
}


void LogicScheduler::reloadConfig(void)
{
  if (debug_enabled) {
    cout << m_logic->name() << ": Reloading configuration" << endl;
  }
  
  loadConfiguration();
  m_logic->processEvent("playMsg schedule_reloaded");
}

string LogicScheduler::getStatus(void) const
{
  stringstream ss;
  ss << "Schedule system " << (enabled ? "enabled" : "disabled") << "\n";
  ss << "Loaded messages: " << messages.size() << "\n";
  
  for (const auto& msg_pair : messages) {
    const ScheduledMessage& msg = msg_pair.second;
    ss << "  " << msg.name << ": Days=";
    for (int day : msg.days) ss << day << " ";
    ss << "Times=";
    for (const string& time : msg.times) ss << time << " ";
    if (!msg.file.empty()) ss << "File=" << msg.file << " ";
    if (msg.macro >= 0) ss << "Macro=" << msg.macro << " ";
    if (!msg.command.empty()) ss << "Command=" << msg.command << " ";
    if (!msg.disabled_logics.empty()) {
      ss << "DisabledIn=";
      for (const string& logic : msg.disabled_logics) ss << logic << ",";
    }
    ss << "\n";
  }
  
  return ss.str();
}

void LogicScheduler::setDebug(bool enable)
{
  debug_enabled = enable;
  m_logic->processEvent(debug_enabled ? "playMsg debug_enabled" : "playMsg debug_disabled");
  cout << m_logic->name() << ": Debug " << (debug_enabled ? "enabled" : "disabled") << endl;
}

void LogicScheduler::triggerMessage(const string& msg_name)
{
  auto it = messages.find(msg_name);
  if (it != messages.end()) {
    executeMessage(it->second);
  } else {
    cout << "Warning: Message '" << msg_name << "' not found" << endl;
    m_logic->processEvent("playMsg message_not_found");
  }
}

void LogicScheduler::showStatus(void)
{
  m_logic->processEvent("playMsg schedule_status");
  cout << getStatus();
}
