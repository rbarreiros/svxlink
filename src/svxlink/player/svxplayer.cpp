/**
@file    svxplayer.cpp
@brief   Main file for the SvxPlayer reflector audio file player
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

#include <termios.h>
#include <popt.h>
#include <locale.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>

#include <string>
#include <iostream>
#include <cerrno>
#include <cstring>
#include <cstdlib>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncCppApplication.h>
#include <AsyncConfig.h>
#include <version/SVXLINK.h>
#include <config.h>
#include <LogWriter.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "SvxPlayer.h"


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

#define PROGRAM_NAME "SvxPlayer"


/****************************************************************************
 *
 * Prototypes
 *
 ****************************************************************************/

static void parse_arguments(int argc, const char** argv);
static void handle_unix_signal(int signum);


/****************************************************************************
 *
 * Local Global Variables
 *
 ****************************************************************************/

namespace {
  char*       pidfile_name = nullptr;
  char*       logfile_name = nullptr;
  char*       runasuser    = nullptr;
  char*       config       = nullptr;
  int         daemonize    = 0;
  int         quiet        = 0;
  SvxPlayer*  player       = nullptr;
  LogWriter   logwriter;
};


/****************************************************************************
 *
 * MAIN
 *
 ****************************************************************************/

int main(int argc, char** argv)
{
  setlocale(LC_ALL, "");

  CppApplication app;
  app.catchUnixSignal(SIGHUP);
  app.catchUnixSignal(SIGINT);
  app.catchUnixSignal(SIGTERM);
  app.unixSignalCaught.connect(sigc::ptr_fun(&handle_unix_signal));

  parse_arguments(argc, const_cast<const char**>(argv));

  if (daemonize && (daemon(1, 0) == -1))
  {
    perror("daemon");
    exit(1);
  }

  if (quiet || (logfile_name != nullptr))
  {
    int devnull = open("/dev/null", O_RDWR);
    if (devnull == -1)
    {
      perror("open(/dev/null)");
      exit(1);
    }

    if (quiet)
    {
      dup2(devnull, STDOUT_FILENO);
    }

    if (logfile_name != nullptr)
    {
      logwriter.setDestinationName(logfile_name);
      if (!quiet)
      {
        logwriter.redirectStdout();
      }
      logwriter.redirectStderr();
      logwriter.start();

      if (dup2(devnull, STDIN_FILENO) == -1)
      {
        perror("dup2(stdin)");
        exit(1);
      }
    }
    close(devnull);
  }

  if (pidfile_name != nullptr)
  {
    FILE* pidfile = fopen(pidfile_name, "w");
    if (pidfile == nullptr)
    {
      char err[256];
      sprintf(err, "fopen(\"%s\")", pidfile_name);
      perror(err);
      fflush(stderr);
      exit(1);
    }
    fprintf(pidfile, "%d\n", getpid());
    fclose(pidfile);
  }

  const char* home_dir = nullptr;
  if (runasuser != nullptr)
  {
    if (initgroups(runasuser, getgid()))
    {
      perror("initgroups");
      exit(1);
    }
    struct passwd* passwd = getpwnam(runasuser);
    if (passwd == nullptr)
    {
      perror("getpwnam");
      exit(1);
    }
    if (setgid(passwd->pw_gid) == -1)
    {
      perror("setgid");
      exit(1);
    }
    if (setuid(passwd->pw_uid) == -1)
    {
      perror("setuid");
      exit(1);
    }
    home_dir = passwd->pw_dir;
  }

  if (home_dir == nullptr)
  {
    home_dir = getenv("HOME");
  }
  if (home_dir == nullptr)
  {
    home_dir = ".";
  }

  Config cfg;
  string cfg_filename;
  if (config != nullptr)
  {
    cfg_filename = string(config);
    if (!cfg.open(cfg_filename))
    {
      cerr << "*** ERROR: Could not open configuration file: "
           << config << endl;
      exit(1);
    }
  }
  else
  {
    cfg_filename = string(home_dir) + "/.svxplayer/svxplayer.conf";
    if (!cfg.open(cfg_filename))
    {
      cfg_filename = SVX_SYSCONF_INSTALL_DIR "/svxplayer.conf";
      if (!cfg.open(cfg_filename))
      {
        cfg_filename = SYSCONF_INSTALL_DIR "/svxplayer.conf";
        if (!cfg.open(cfg_filename))
        {
          cerr << "*** ERROR: Could not open configuration file";
          if (errno != 0)
          {
            cerr << " (" << strerror(errno) << ")";
          }
          cerr << ".\n"
               << "Tried the following paths:\n"
               << "\t" << home_dir << "/.svxplayer/svxplayer.conf\n"
               << "\t" SVX_SYSCONF_INSTALL_DIR "/svxplayer.conf\n"
               << "\t" SYSCONF_INSTALL_DIR "/svxplayer.conf\n"
               << "Possible reasons for failure are: None of the files exist,\n"
               << "you do not have permission to read the file or there was a\n"
               << "syntax error in the file.\n";
          exit(1);
        }
      }
    }
  }
  string main_cfg_filename(cfg_filename);

  string cfg_dir;
  if (cfg.getValue("GLOBAL", "CFG_DIR", cfg_dir))
  {
    if (cfg_dir[0] != '/')
    {
      int slash_pos = main_cfg_filename.rfind('/');
      if (slash_pos != -1)
      {
        cfg_dir = main_cfg_filename.substr(0, slash_pos + 1) + cfg_dir;
      }
      else
      {
        cfg_dir = string("./") + cfg_dir;
      }
    }

    DIR* dir = opendir(cfg_dir.c_str());
    if (dir == nullptr)
    {
      cerr << "*** ERROR: Could not read directory specified by "
           << "GLOBAL/CFG_DIR=" << cfg_dir << endl;
      exit(1);
    }

    struct dirent* dirent;
    while ((dirent = readdir(dir)) != nullptr)
    {
      const char* dot = strrchr(dirent->d_name, '.');
      if ((dot == nullptr) || (dirent->d_name[0] == '.') ||
          (strcmp(dot, ".conf") != 0))
      {
        continue;
      }
      cfg_filename = cfg_dir + "/" + dirent->d_name;
      if (!cfg.open(cfg_filename))
      {
        cerr << "*** ERROR: Could not open configuration file: "
             << cfg_filename << endl;
        exit(1);
      }
    }

    if (closedir(dir) == -1)
    {
      cerr << "*** ERROR: Error closing directory specified by "
           << "GLOBAL/CFG_DIR=" << cfg_dir << endl;
      exit(1);
    }
  }

  string tstamp_format = "%c";
  cfg.getValue("GLOBAL", "TIMESTAMP_FORMAT", tstamp_format);
  logwriter.setTimestampFormat(tstamp_format);

  cout << PROGRAM_NAME " v" SVXLINK_APP_VERSION
          " Copyright (C) 2003-2026 Tobias Blomberg / SM0SVX\n\n";
  cout << PROGRAM_NAME " comes with ABSOLUTELY NO WARRANTY. "
          "This is free software, and you are\n";
  cout << "welcome to redistribute it in accordance with the terms "
          "and conditions in the\n";
  cout << "GNU GPL (General Public License) version 2 or later.\n";
  cout << "\nUsing configuration file: " << main_cfg_filename << "\n" << endl;

  player = new SvxPlayer;
  if (!player->initialize(cfg))
  {
    cerr << "*** ERROR: Failed to initialize SvxPlayer" << endl;
    delete player;
    exit(1);
  }

  app.exec();

  delete player;
  player = nullptr;

  return 0;
} /* main */


/****************************************************************************
 *
 * Functions
 *
 ****************************************************************************/

static void parse_arguments(int argc, const char** argv)
{
  int print_version = 0;

  struct poptOption options[] =
  {
    POPT_AUTOHELP
    {
      "pidfile", 0, POPT_ARG_STRING, &pidfile_name,
      0, "Specify the name of the PID file to use", "<filename>"
    },
    {
      "logfile", 0, POPT_ARG_STRING, &logfile_name,
      0, "Specify the name of the log file to use", "<filename>"
    },
    {
      "runasuser", 0, POPT_ARG_STRING, &runasuser,
      0, "Specify the user to run svxplayer as", "<username>"
    },
    {
      "config", 0, POPT_ARG_STRING, &config,
      0, "Specify the configuration file to use", "<filename>"
    },
    {
      "daemon", 0, POPT_ARG_NONE, &daemonize,
      0, "Start svxplayer as a daemon", nullptr
    },
    {
      "quiet", 0, POPT_ARG_NONE, &quiet,
      0, "Suppress output to stdout", nullptr
    },
    {
      "version", 0, POPT_ARG_NONE, &print_version,
      0, "Print the version and exit", nullptr
    },
    POPT_TABLEEND
  };

  poptContext ctx = poptGetContext(nullptr, argc, argv, options, 0);
  poptSetOtherOptionHelp(ctx, "[OPTIONS]");

  int rc;
  while ((rc = poptGetNextOpt(ctx)) >= 0)
  {
  }

  if (rc < -1)
  {
    cerr << poptBadOption(ctx, POPT_BADOPTION_NOALIAS) << ": "
         << poptStrerror(rc) << "\n";
    poptFreeContext(ctx);
    exit(2);
  }

  poptFreeContext(ctx);

  if (print_version)
  {
    cout << SVXLINK_VERSION << endl;
    exit(0);
  }
} /* parse_arguments */


static void handle_unix_signal(int signum)
{
  switch (signum)
  {
    case SIGTERM:
    case SIGINT:
      cout << PROGRAM_NAME ": Received signal " << signum
           << ". Shutting down." << endl;
      Application::app().quit();
      break;

    case SIGHUP:
      logwriter.reopenLogfile();
      break;
  }
} /* handle_unix_signal */


/*
 * This file has not been truncated
 */
