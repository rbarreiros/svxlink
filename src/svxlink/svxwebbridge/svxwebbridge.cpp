/**
@file    svxwebbridge.cpp
@brief   Main source file for the SvxWebBridge application
@author  Rui Barreiros / CR7BPM

svxwebbridge connects to an SvxReflector as an authenticated node,
then exposes a WebSocket server so browser clients can listen to any
talk group and receive real-time audio and talker-start/stop metadata.
Audio sent through the websocket is Opus encoded.

\verbatim
SvxLink - A Multi Purpose Voice Services System for Ham Radio Use
Copyright (C) 2003-2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
\endverbatim
*/


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <locale.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <grp.h>
#include <pwd.h>
#include <errno.h>

#include <popt.h>
#include <sigc++/sigc++.h>

#include <cstdio>
#include <cstring>
#include <iostream>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncCppApplication.h>
#include <AsyncFdWatch.h>
#include <AsyncConfig.h>
#include <config.h>
#include <LogWriter.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "WebBridge.h"


/****************************************************************************
 *
 * Namespaces
 *
 ****************************************************************************/

using namespace std;
using namespace Async;


/****************************************************************************
 *
 * Defines
 *
 ****************************************************************************/

#define PROGRAM_NAME    "SvxWebBridge"
#define CFG_SECTION     "SVXWEBBRIDGE"


/****************************************************************************
 *
 * Local prototypes
 *
 ****************************************************************************/

static void parse_arguments(int argc, const char** argv);
static void stdinHandler(FdWatch* w);
static void handle_unix_signal(int signum);


/****************************************************************************
 *
 * File-local globals
 *
 ****************************************************************************/

namespace {
  char*       pidfile_name  = nullptr;
  char*       logfile_name  = nullptr;
  char*       runasuser     = nullptr;
  char*       config        = nullptr;
  int         daemonize     = 0;
  int         quiet         = 0;
  FdWatch*    stdin_watch   = nullptr;
  LogWriter   logwriter;
  WebBridge*  bridge        = nullptr;
}


/****************************************************************************
 *
 * main
 *
 ****************************************************************************/

int main(int argc, const char* argv[])
{
  setlocale(LC_ALL, "");

  CppApplication app;
  app.catchUnixSignal(SIGHUP);
  app.catchUnixSignal(SIGINT);
  app.catchUnixSignal(SIGTERM);
  app.unixSignalCaught.connect(sigc::ptr_fun(&handle_unix_signal));

  parse_arguments(argc, argv);

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
      if (!quiet) { logwriter.redirectStdout(); }
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
      snprintf(err, sizeof(err), "fopen(\"%s\")", pidfile_name);
      perror(err);
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
    struct passwd* pw = getpwnam(runasuser);
    if (pw == nullptr) { perror("getpwnam"); exit(1); }
    if (setgid(pw->pw_gid) == -1) { perror("setgid"); exit(1); }
    if (setuid(pw->pw_uid) == -1)  { perror("setuid"); exit(1); }
    home_dir = pw->pw_dir;
  }
  if (home_dir == nullptr) { home_dir = getenv("HOME"); }
  if (home_dir == nullptr) { home_dir = "."; }

  Config cfg;
  string cfg_filename;
  if (config != nullptr)
  {
    cfg_filename = config;
    if (!cfg.open(cfg_filename))
    {
      cerr << "*** ERROR: Could not open configuration file: "
           << config << endl;
      exit(1);
    }
  }
  else
  {
    cfg_filename = string(home_dir) + "/.svxlink/svxwebbridge.conf";
    if (!cfg.open(cfg_filename))
    {
      cfg_filename = string(SVX_SYSCONF_INSTALL_DIR) + "/svxwebbridge.conf";
      if (!cfg.open(cfg_filename))
      {
        cerr << "*** ERROR: Could not open configuration file";
        if (errno != 0) { cerr << " (" << strerror(errno) << ")"; }
        cerr << ".\nTried:\n"
             << "\t" << home_dir << "/.svxlink/svxwebbridge.conf\n"
             << "\t" << SVX_SYSCONF_INSTALL_DIR << "/svxwebbridge.conf\n";
        exit(1);
      }
    }
  }

  cerr << PROGRAM_NAME " v" PROJECT_VERSION
       << " Copyright (C) 2003-2026 by contributors\n"
       << "SvxLink comes with ABSOLUTELY NO WARRANTY. "
          "This is free software, and you\n"
          "are welcome to redistribute it in accordance with the terms "
          "and conditions of\nthe GNU General Public License (GPL) version 2 "
          "or later.\n";

  cout << "Starting " PROGRAM_NAME " v" PROJECT_VERSION << "\n"
       << "Configuration file: " << cfg_filename << "\n"
       << "------- Loading configuration -------\n";

  bridge = new WebBridge;
  if (!bridge->initialize(cfg, CFG_SECTION))
  {
    cerr << "*** ERROR: Failed to initialize WebBridge" << endl;
    delete bridge;
    exit(1);
  }

  if (!daemonize && (logfile_name == nullptr))
  {
      // Watch stdin so Ctrl-D exits gracefully
    stdin_watch = new FdWatch(STDIN_FILENO, FdWatch::FD_WATCH_RD);
    stdin_watch->activity.connect(sigc::ptr_fun(&stdinHandler));
  }

  cout << "------- " PROGRAM_NAME " started -------\n";
  app.exec();

  delete stdin_watch;
  stdin_watch = nullptr;
  delete bridge;
  bridge = nullptr;

  if (pidfile_name != nullptr) { remove(pidfile_name); }

  return 0;
} /* main */


/****************************************************************************
 *
 * Local functions
 *
 ****************************************************************************/

static void parse_arguments(int argc, const char** argv)
{
  struct poptOption options[] =
  {
    {"pidfile",   'p', POPT_ARG_STRING, &pidfile_name, 0,
     "Specify the name of the pidfile to use", "<filename>"},
    {"logfile",   'l', POPT_ARG_STRING, &logfile_name, 0,
     "Specify the name of the logfile to use", "<filename>"},
    {"runasuser", 'u', POPT_ARG_STRING, &runasuser, 0,
     "Run as the given user", "<username>"},
    {"config",    'f', POPT_ARG_STRING, &config, 0,
     "Specify the configuration file to use", "<filename>"},
    {"daemon",    'd', POPT_ARG_NONE,   &daemonize, 0,
     "Start as a daemon", nullptr},
    {"quiet",     'q', POPT_ARG_NONE,   &quiet, 0,
     "Suppress output to stdout", nullptr},
    POPT_AUTOHELP
    {nullptr, 0, 0, nullptr, 0, nullptr, nullptr}
  };

  poptContext opt_ctx = poptGetContext(nullptr, argc, argv, options, 0);
  poptSetOtherOptionHelp(opt_ctx, "[OPTIONS]");
  int c;
  while ((c = poptGetNextOpt(opt_ctx)) >= 0) {}
  if (c < -1)
  {
    cerr << poptBadOption(opt_ctx, POPT_BADOPTION_NOALIAS) << ": "
         << poptStrerror(c) << endl;
    exit(1);
  }
  poptFreeContext(opt_ctx);
} /* parse_arguments */


static void stdinHandler(FdWatch*)
{
  char buf[256];
  ssize_t cnt = read(STDIN_FILENO, buf, sizeof(buf) - 1);
  if (cnt == 0)
  {
    cerr << "EOF on stdin; exiting." << endl;
    Application::app().quit();
  }
} /* stdinHandler */


static void handle_unix_signal(int signum)
{
  switch (signum)
  {
    case SIGTERM:
    case SIGINT:
      cout << "Received signal " << signum << "; shutting down." << endl;
      Application::app().quit();
      break;
    case SIGHUP:
      cout << "Received SIGHUP; reload not implemented." << endl;
      break;
    default:
      break;
  }
} /* handle_unix_signal */


/*
 * This file has not been truncated
 */
