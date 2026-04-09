/**
@file    echoclient.cpp
@brief   Standalone EchoLink ↔ SvxReflector client – main entry point
@author  Rui Barreiros / CR7BPM

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

#include <popt.h>
#include <locale.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <string>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <cerrno>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncCppApplication.h>
#include <AsyncConfig.h>
#include <version/SVXLINK.h>
#include <config.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "EchoClient.h"


/****************************************************************************
 *
 * Namespaces to use
 *
 ****************************************************************************/

using namespace std;
using namespace Async;


/****************************************************************************
 *
 * Local globals
 *
 ****************************************************************************/

static CppApplication* app = nullptr;


/****************************************************************************
 *
 * Signal handlers
 *
 ****************************************************************************/

static void sigterm_handler(int)
{
  if (app != nullptr)
  {
    app->quit();
  }
} /* sigterm_handler */


/****************************************************************************
 *
 * main()
 *
 ****************************************************************************/

int main(int argc, const char* argv[])
{
  setlocale(LC_ALL, "");

    // ---- Command-line parsing ------------------------------------------------
  const char* config_file  = nullptr;
  const char* log_file     = nullptr;
  const char* run_as_user  = nullptr;
  const char* run_as_group = nullptr;
  const char* section_name = "EchoClient";
  int         version_flag = 0;

  struct poptOption options[] = {
    { "config",    'c', POPT_ARG_STRING, &config_file,  0,
      "Path to configuration file", "FILE" },
    { "log",       'l', POPT_ARG_STRING, &log_file,     0,
      "Log to FILE instead of stdout", "FILE" },
    { "section",   's', POPT_ARG_STRING, &section_name, 0,
      "Config section name (default: EchoClient)", "SECTION" },
    { "user",       0,  POPT_ARG_STRING, &run_as_user,  0,
      "Drop privileges to USER", "USER" },
    { "group",      0,  POPT_ARG_STRING, &run_as_group, 0,
      "Drop privileges to GROUP", "GROUP" },
    { "version",   'v', POPT_ARG_NONE,   &version_flag, 0,
      "Print version and exit", nullptr },
    POPT_AUTOHELP
    POPT_TABLEEND
  };

  poptContext opt_ctx = poptGetContext(nullptr, argc, argv, options, 0);
  int opt_ret;
  while ((opt_ret = poptGetNextOpt(opt_ctx)) >= 0) {}
  if (opt_ret < -1)
  {
    cerr << poptBadOption(opt_ctx, POPT_BADOPTION_NOALIAS)
         << ": " << poptStrerror(opt_ret) << endl;
    poptFreeContext(opt_ctx);
    return 1;
  }
  poptFreeContext(opt_ctx);

  if (version_flag)
  {
    cout << "echoclient v" << SVXLINK_APP_VERSION
         << " (" << PROJECT_VERSION << ")\n";
    return 0;
  }

  if (config_file == nullptr)
  {
    cerr << "*** ERROR: --config must be specified\n";
    return 1;
  }

    // ---- Optional log file --------------------------------------------------
  if (log_file != nullptr)
  {
    int fd = open(log_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
    {
      cerr << "*** ERROR: Cannot open log file \"" << log_file
           << "\": " << strerror(errno) << "\n";
      return 1;
    }
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);
  }

    // ---- Signal handling ----------------------------------------------------
  signal(SIGTERM, sigterm_handler);
  signal(SIGINT,  sigterm_handler);
  signal(SIGPIPE, SIG_IGN);

    // ---- Privilege drop -----------------------------------------------------
  if (run_as_group != nullptr)
  {
    struct group* gr = getgrnam(run_as_group);
    if (gr == nullptr)
    {
      cerr << "*** ERROR: Group \"" << run_as_group << "\" not found\n";
      return 1;
    }
    if (setgid(gr->gr_gid) != 0)
    {
      cerr << "*** ERROR: setgid: " << strerror(errno) << "\n";
      return 1;
    }
  }
  if (run_as_user != nullptr)
  {
    struct passwd* pw = getpwnam(run_as_user);
    if (pw == nullptr)
    {
      cerr << "*** ERROR: User \"" << run_as_user << "\" not found\n";
      return 1;
    }
    if (setuid(pw->pw_uid) != 0)
    {
      cerr << "*** ERROR: setuid: " << strerror(errno) << "\n";
      return 1;
    }
  }

    // ---- Configuration ------------------------------------------------------
  CppApplication the_app;
  app = &the_app;

  Config cfg;
  if (!cfg.open(config_file))
  {
    cerr << "*** ERROR: Cannot open config file \"" << config_file << "\"\n";
    return 1;
  }

    // Optional GLOBAL/CFG_DIR inclusion
  string cfg_dir;
  if (cfg.getValue("GLOBAL", "CFG_DIR", cfg_dir) && !cfg_dir.empty())
  {
    if (!cfg.open(cfg_dir))
    {
      cerr << "*** WARNING: Could not open CFG_DIR \"" << cfg_dir << "\"\n";
    }
  }

    // ---- Start the EchoLink client ------------------------------------------
  EchoClient client;
  if (!client.initialize(cfg, section_name))
  {
    cerr << "*** ERROR: EchoClient initialization failed\n";
    return 1;
  }

  cout << "EchoClient started. Press Ctrl+C to exit.\n";
  the_app.exec();
  cout << "EchoClient shutting down.\n";

  return 0;
} /* main */


/*
 * This file has not been truncated
 */
