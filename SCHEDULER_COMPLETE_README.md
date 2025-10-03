# SvxLink Scheduler System

This document describes the C++-based scheduler implementation for SvxLink, providing flexible scheduled message functionality.

## Overview

The SvxLink Scheduler System allows you to automatically play audio files or execute macros at specific times and days. The scheduler is integrated directly into SvxLink core as a C++ component.

## Features

- **Flexible Time Formats**: Support for specific times, intervals, and multiple time slots
- **Flexible Day Formats**: Support for specific days, day ranges, and special keywords
- **Multiple Message Types**: Play audio files or execute macros
- **Hour Intervals**: Support for `*/2` (every 2 hours), `*/4` (every 4 hours), etc.
- **Debug Support**: Built-in debugging and status reporting
- **DTMF Control**: Control the system via DTMF commands
- **Real-time Configuration**: Reload configuration without restart (C++ version)

## Time Formats

### Specific Times
- `12:00` - 12:00 PM (24-hour format)
- `08:30` - 8:30 AM
- `23:45` - 11:45 PM

### Multiple Times
- `08:00,12:00,18:00` - 8 AM, 12 PM, and 6 PM
- `09:00,15:00,21:00` - 9 AM, 3 PM, and 9 PM

### Minute Intervals
- `*/15` - Every 15 minutes
- `*/30` - Every 30 minutes
- `*/60` - Every hour

### Hour Intervals (NEW!)
- `*/2` - Every 2 hours (00:00, 02:00, 04:00, etc.)
- `*/4` - Every 4 hours (00:00, 04:00, 08:00, etc.)
- `*/6` - Every 6 hours (00:00, 06:00, 12:00, 18:00)

### Hourly Intervals
- `09:*/15` - Every 15 minutes during the 9 AM hour
- `12:*/30` - Every 30 minutes during the 12 PM hour

### Mixed Formats
- `08:00,*/2` - At 8:00 AM and every 2 hours
- `12:00,18:00,*/4` - At 12:00 PM, 6:00 PM, and every 4 hours

## Day Formats

### All Days
- `ALL` - Every day of the week

### Specific Days (Multiple Formats)
- `MON,TUE,WED,THU,FRI` - Monday through Friday
- `MONDAY,TUESDAY,WEDNESDAY,THURSDAY,FRIDAY` - Full day names
- `1,2,3,4,5` - Numeric format (1=Monday, 7=Sunday)

### Special Keywords
- `WEEKDAYS` - Monday through Friday
- `WEEKENDS` - Saturday and Sunday

## Logic-Specific Control

The scheduler supports fine-grained control over which logics can play scheduled messages using the `DISABLE_LOGIC` parameter.

### DISABLE_LOGIC Parameter

- **Purpose**: Specify which logics should NOT play a particular scheduled message
- **Format**: Comma-separated list of logic names
- **Default**: If not specified, the message plays in all logics

### Examples

```ini
# Play in all logics (default behavior)
[WeatherMessage]
DAYS=ALL
TIME=12:00
FILE=/usr/share/svxlink/sounds/weather.wav

# Play in all logics except RepeaterLogic
[NewsRadio]
DAYS=WEEKDAYS
TIME=08:00,18:00
MACRO=9
DISABLE_LOGIC=RepeaterLogic

# Play only in SimplexLogic and ReflectorLogic
[IDMessage]
DAYS=WEEKENDS
TIME=*/30
FILE=/usr/share/svxlink/sounds/station_id.wav
DISABLE_LOGIC=RepeaterLogic,AnnounceLogic

# Play only in RepeaterLogic
[RepeaterOnly]
DAYS=ALL
TIME=06:00,12:00,18:00
FILE=/usr/share/svxlink/sounds/repeater_info.wav
DISABLE_LOGIC=SimplexLogic,ReflectorLogic,AnnounceLogic
```

### Use Cases

1. **Repeater-specific messages**: Play only on repeater logic
2. **Simplex-specific messages**: Play only on simplex logic
3. **Reflector-specific messages**: Play only on reflector logic
4. **Exclude specific logics**: Play in most logics but exclude certain ones
5. **Mixed configurations**: Different messages for different logics

## Installation

The scheduler is integrated directly into SvxLink core and requires building from source:

1. The scheduler is automatically included when you build SvxLink
2. No additional configuration needed - it's always available
3. Add schedule configuration to your `svxlink.conf` file

## Configuration

Add a `[SCHEDULE]` section to your `svxlink.conf`:

```ini
[SCHEDULE]
ENABLED=1
DEBUG=1
MESSAGES=WeatherMessage,NewsRadio,IDMessage

[WeatherMessage]
DAYS=ALL
TIME=12:00
FILE=/usr/share/svxlink/sounds/weather.wav
ENABLED=1

[NewsRadio]
DAYS=WEEKDAYS
TIME=08:00,18:00
MACRO=9
ENABLED=1
```

## DTMF Commands

- `0` - Show schedule status
- `1` - Reload schedule configuration
- `2` - Toggle debug mode
- `99<message_name>` - Manually trigger a specific message

## Message Types

### Audio Files
```ini
[WeatherMessage]
DAYS=ALL
TIME=12:00
FILE=/usr/share/svxlink/sounds/weather.wav
```

### Macros
```ini
[NewsRadio]
DAYS=WEEKDAYS
TIME=08:00,18:00
MACRO=9
```

**Note**: Macros are executed using the `MACRO_PREFIX` (default "D") + macro number. For example, `MACRO=9` executes `D9` which runs the macro defined in your `[Macros]` section.

## MACRO Parameter

The `MACRO` parameter allows you to execute predefined macros from your `svxlink.conf` at scheduled times.

### Macro Execution
```ini
[EchoLinkConnect]
DAYS=ALL
TIME=08:00
MACRO=1
ENABLED=1
```

### Macro Configuration
Make sure your macros are defined in the `[Macros]` section of your `svxlink.conf`:
```ini
[Macros]
1=EchoLink:9999#
9=Parrot:0123456789#
03400=EchoLink:9999#
```

### Macro Prefix
The scheduler uses the `MACRO_PREFIX` setting (default "D") to construct the macro command:
- `MACRO=9` → executes `D9`
- `MACRO=1` → executes `D1`
- `MACRO=03400` → executes `D03400`

### Macro Execution Notes
- Macros are executed by calling `Logic::processMacroCmd()`
- The macro prefix is configurable via `GLOBAL.MACRO_PREFIX`
- Macros run with the same privileges as manual DTMF commands
- Complex macros (like `03400`) are fully supported

### System Commands
```ini
[SystemCheck]
DAYS=ALL
TIME=02:00
COMMAND=/usr/local/bin/system_health_check.sh
ENABLED=1
```

## COMMAND Parameter

The `COMMAND` parameter allows you to execute system commands at scheduled times. This is similar to `EXEC_CMD_ON_SQL_CLOSE` but for scheduled events.

### Basic Command Execution
```ini
[LogRotate]
DAYS=7
TIME=03:00
COMMAND=/usr/bin/logrotate /etc/logrotate.d/svxlink
ENABLED=1
```

### Complex Commands
You can execute complex commands using shell features:
```ini
[Maintenance]
DAYS=ALL
TIME=04:00
COMMAND=/bin/bash -c "echo 'Starting maintenance at $(date)' >> /var/log/svxlink/maintenance.log && /usr/bin/find /var/log/svxlink -name '*.log' -mtime +30 -delete"
ENABLED=1
```

### Combined Actions
You can combine commands with audio files or macros:
```ini
[StatusReport]
DAYS=WEEKDAYS
TIME=09:00
COMMAND=/usr/local/bin/generate_status_report.sh
FILE=/usr/share/svxlink/sounds/en_US/Help/choose_module.wav
ENABLED=1
```

### Command Placeholders
Commands support placeholders that are replaced with dynamic values at execution time:

| Placeholder | Description | Example Output |
|-------------|-------------|----------------|
| `%LOGIC%` | Current logic name | `RepeaterLogic` |
| `%DATE%` | Current date (YYYY-MM-DD) | `2024-01-15` |
| `%TIME%` | Current time (HH:MM:SS) | `14:30:25` |
| `%DATETIME%` | Date and time (YYYY-MM-DD HH:MM:SS) | `2024-01-15 14:30:25` |

**Example with placeholders:**
```ini
[LogMessage]
DAYS=ALL
TIME=*/5
COMMAND=echo "[%DATETIME%] %LOGIC%: Scheduled message executed" >> /var/log/svxlink/scheduler.log
ENABLED=1
```

**Expanded command output:**
```bash
echo "[2024-01-15 14:30:25] RepeaterLogic: Scheduled message executed" >> /var/log/svxlink/scheduler.log
```

### Command Execution Notes
- Commands are executed in the background using `system()`
- Exit codes are logged when debug mode is enabled
- Commands run with the same privileges as the SvxLink process
- Use absolute paths for commands and scripts
- Consider security implications when executing external commands
- Placeholders are replaced before command execution

### Logic-Specific Control
```ini
[RepeaterOnly]
DAYS=ALL
TIME=06:00,12:00,18:00
FILE=/usr/share/svxlink/sounds/repeater_info.wav
DISABLE_LOGIC=SimplexLogic,ReflectorLogic

[SimplexOnly]
DAYS=ALL
TIME=07:00,13:00,19:00
FILE=/usr/share/svxlink/sounds/simplex_info.wav
DISABLE_LOGIC=RepeaterLogic,ReflectorLogic
```

**Note**: Direct macro execution is not supported in either implementation. When a macro is specified, the system will play a "macro" message followed by the macro number instead of executing the actual macro.

## Advanced Configuration Examples

### Every 2 Hours
```ini
[TwoHourly]
DAYS=ALL
TIME=*/2
FILE=/usr/share/svxlink/sounds/two_hourly.wav
ENABLED=1
```

### Every 4 Hours During Business Hours
```ini
[BusinessHours]
DAYS=MON,TUE,WED,THU,FRI
TIME=08:00,12:00,16:00
FILE=/usr/share/svxlink/sounds/business.wav
ENABLED=1
```

### Mixed Schedule
```ini
[MixedSchedule]
DAYS=ALL
TIME=08:00,12:00,18:00,*/4
FILE=/usr/share/svxlink/sounds/mixed.wav
ENABLED=1
```

### Every 15 Minutes During Morning Hours
```ini
[MorningUpdates]
DAYS=MON,TUE,WED,THU,FRI
TIME=06:*/15,07:*/15,08:*/15,09:*/15
MACRO=5
ENABLED=1
```

## Performance Benefits

The C++ implementation provides several advantages:

1. **Better Performance**: Native C++ code runs faster than TCL
2. **More Reliable**: Direct integration with SvxLink's timer system
3. **Lower Resource Usage**: No TCL interpreter overhead
4. **Real-time Updates**: Configuration changes take effect immediately
5. **Better Error Handling**: More robust error detection and reporting
6. **Always Available**: Runs regardless of which module is active
7. **Full Hour Interval Support**: Complete support for all time formats

## Troubleshooting

### Common Issues

1. **Messages not playing**: Check that the audio file exists and is readable
2. **Wrong times**: Verify time format (24-hour format required)
3. **Wrong days**: Check day format (case-insensitive)
4. **Configuration not loading**: Ensure the `[SCHEDULE]` section exists in your config
5. **Macros not executing**: This is expected - see limitations above

### Debug Steps

1. Enable debug mode: Use DTMF commands or set `DEBUG=1` in configuration
2. Check status: Use DTMF commands to see loaded messages
3. Reload config: Use DTMF commands to reload configuration
4. Check logs: Look for scheduler-related messages in SvxLink logs

### Debug Output

When debug mode is enabled, you'll see output like:
```
Scheduler: Checking scheduled messages at 12:00 (day 1)
Scheduler: Executing scheduled message: WeatherMessage
Scheduler: Played file: /usr/share/svxlink/sounds/weather.wav
```

## Migration Between Implementations

### From TCL to C++
1. Remove `sourceTclWithOverrides "schedule.tcl"` from your TCL files
2. Build SvxLink with the C++ scheduler
3. Update your configuration if needed
4. Test the new implementation

### From C++ to TCL
1. Install the TCL scheduler files
2. Add `sourceTclWithOverrides "schedule.tcl"` to your TCL files
3. Test the TCL implementation

The configuration format is largely compatible between versions, so minimal changes should be needed.

## License

This code is provided as-is for use with SvxLink. Please ensure compliance with your local regulations when using automated announcements on amateur radio frequencies.

## Support

For issues or questions:
1. Check the debug output for error messages
2. Verify your configuration syntax
3. Test with simple configurations first
4. Check SvxLink logs for additional information
