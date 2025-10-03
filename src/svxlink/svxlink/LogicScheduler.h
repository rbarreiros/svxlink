#ifndef LOGIC_SCHEDULER_H
#define LOGIC_SCHEDULER_H

#include <AsyncConfig.h>
#include <AsyncTimer.h>
#include <AsyncAtTimer.h>
#include <string>
#include <vector>
#include <map>
#include <set>

class Logic;

/**
@brief	Scheduler component for Logic class
@author Your Name
@date   2024

This class provides scheduled message functionality that integrates directly
into the Logic class. It can play audio files or execute macros at
specified times and days.
*/
class LogicScheduler : public sigc::trackable
{
  public:
    /**
     * @brief 	Constructor
     * @param 	logic Pointer to the Logic instance
     */
    LogicScheduler(Logic *logic);
    
    /**
     * @brief 	Destructor
     */
    ~LogicScheduler(void);
    
    /**
     * @brief 	Initialize the scheduler
     * @return	Returns \em true on success or \em false on failure
     */
    bool initialize(void);
    
    /**
     * @brief 	Check for scheduled messages (called every minute)
     */
    void checkScheduledMessages(void);
    
    /**
     * @brief 	Reload configuration
     */
    void reloadConfig(void);
    
    /**
     * @brief 	Get status information
     * @return	Returns status string
     */
    std::string getStatus(void) const;
    
    /**
     * @brief 	Set debug mode
     * @param 	enable Set to \em true to enable debug output
     */
    void setDebug(bool enable);
    
    /**
     * @brief 	Trigger a specific message manually
     * @param 	message_name Name of the message to trigger
     */
    void triggerMessage(const std::string& message_name);
    
    /**
     * @brief 	Check if scheduler is enabled
     * @return	Returns \em true if enabled or \em false if disabled
     */
    bool isEnabled(void) const { return enabled; }
    
    /**
     * @brief 	Show scheduler status (public interface)
     */
    void showStatus(void);
    
  private:
    struct ScheduledMessage
    {
      std::string name;
      std::set<int> days;        // 1=Monday, 7=Sunday
      std::set<std::string> times; // HH:MM format
      std::string file;
      int macro;
      std::string command;       // System command to execute
      bool enabled;
      std::set<std::string> disabled_logics; // Logics where this message should not play
      
      ScheduledMessage() : macro(-1), enabled(true) {}
    };
    
    struct TimeInterval
    {
      int hour;
      int minute;
      int interval_minutes;
      bool is_interval;
      
      TimeInterval() : hour(0), minute(0), interval_minutes(0), is_interval(false) {}
    };
    
    Logic *m_logic;
    std::map<std::string, ScheduledMessage> messages;
    std::vector<TimeInterval> time_intervals;
    bool enabled;
    bool debug_enabled;
    std::string macro_prefix;
    
    void loadConfiguration(void);
    void parseDays(const std::string& days_str, std::set<int>& days);
    void parseTimes(const std::string& times_str, std::set<std::string>& times);
    void parseTimeInterval(const std::string& time_str, TimeInterval& interval);
    void parseDisabledLogics(const std::string& disabled_logics_str, std::set<std::string>& disabled_logics);
    bool shouldPlayInCurrentLogic(const ScheduledMessage& msg);
    void executeMessage(const ScheduledMessage& msg);
    void playFile(const std::string& filename);
    void executeMacro(int macro_num);
    void executeCommand(const std::string& command);
    std::string expandCommandPlaceholders(const std::string& command);
    std::string getCurrentTime(void);
    int getCurrentDay(void);
    bool isTimeMatch(const std::string& current_time, const std::set<std::string>& scheduled_times);
    bool isDayMatch(int current_day, const std::set<int>& scheduled_days);
    
    // DTMF command handlers
    void toggleDebug(void);
};

#endif // LOGIC_SCHEDULER_H
