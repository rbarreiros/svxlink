/**
@file	 svxlink_totp_config.cpp
@brief   TOTP configuration tool for SVXLink
@author  Generated for SVXLink TOTP Implementation
@date	 2025-09-17

This utility generates TOTP secrets and provides configuration snippets for
svxlink.conf. It's compatible with Google Authenticator and other TOTP apps.

\verbatim
SvxLink - A Multi Purpose Voice Services System for Ham Radio Use
Copyright (C) 2025

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

#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdlib>
#include <getopt.h>
#include <qrencode.h>

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

#include "TOTPAuth.h"

/****************************************************************************
 *
 * Namespaces to use
 *
 ****************************************************************************/

using namespace std;

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

static void printUsage(const char* program_name);
static void printQRCode(const string& data);
static void generateConfig(const string& callsign, const string& logic_name,
                          int auth_timeout, bool multi_user_format);
static void addUser(const string& user_id, const string& user_name);
static void testCode(const string& secret, const string& code);
static void showCurrentCode(const string& secret);

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
 * Public member functions
 *
 ****************************************************************************/

int main(int argc, char* argv[])
{
  string callsign;
  string logic_name = "SimplexLogic";
  string secret;
  string test_code;
  string user_id;
  string user_name;
  int auth_timeout = 300;
  bool generate_new = false;
  bool show_current = false;
  bool test_mode = false;
  bool add_user_mode = false;
  bool multi_user_format = false;

  static struct option long_options[] = {
    {"callsign",     required_argument, 0, 'c'},
    {"logic",        required_argument, 0, 'l'},
    {"secret",       required_argument, 0, 's'},
    {"timeout",      required_argument, 0, 't'},
    {"test",         required_argument, 0, 'T'},
    {"current",      no_argument,       0, 'C'},
    {"generate",     no_argument,       0, 'g'},
    {"add-user",     no_argument,       0, 'a'},
    {"user-id",      required_argument, 0, 'u'},
    {"user-name",    required_argument, 0, 'n'},
    {"multi-user",   no_argument,       0, 'm'},
    {"help",         no_argument,       0, 'h'},
    {0, 0, 0, 0}
  };

  int option_index = 0;
  int c;

  while ((c = getopt_long(argc, argv, "c:l:s:t:T:Cgau:n:mh", long_options, &option_index)) != -1)
  {
    switch (c)
    {
      case 'c':
        callsign = optarg;
        break;
      case 'l':
        logic_name = optarg;
        break;
      case 's':
        secret = optarg;
        break;
      case 't':
        auth_timeout = atoi(optarg);
        break;
      case 'T':
        test_code = optarg;
        test_mode = true;
        break;
      case 'C':
        show_current = true;
        break;
      case 'g':
        generate_new = true;
        break;
      case 'a':
        add_user_mode = true;
        break;
      case 'u':
        user_id = optarg;
        break;
      case 'n':
        user_name = optarg;
        break;
      case 'm':
        multi_user_format = true;
        break;
      case 'h':
        printUsage(argv[0]);
        return 0;
      case '?':
        printUsage(argv[0]);
        return 1;
      default:
        printUsage(argv[0]);
        return 1;
    }
  }

  // Validate required arguments
  if (callsign.empty() && generate_new)
  {
    cerr << "*** ERROR: Callsign is required when generating new configuration" << endl;
    printUsage(argv[0]);
    return 1;
  }

  if ((test_mode || show_current) && secret.empty())
  {
    cerr << "*** ERROR: Secret is required for test or current code display" << endl;
    printUsage(argv[0]);
    return 1;
  }

  try
  {
    if (generate_new)
    {
      generateConfig(callsign, logic_name, auth_timeout, multi_user_format);
    }
    else if (add_user_mode)
    {
      if (user_id.empty())
      {
        cerr << "*** ERROR: User ID is required when adding a user" << endl;
        printUsage(argv[0]);
        return 1;
      }
      addUser(user_id, user_name.empty() ? user_id : user_name);
    }
    else if (test_mode)
    {
      testCode(secret, test_code);
    }
    else if (show_current)
    {
      showCurrentCode(secret);
    }
    else
    {
      printUsage(argv[0]);
      return 1;
    }
  }
  catch (const exception& e)
  {
    cerr << "*** ERROR: " << e.what() << endl;
    return 1;
  }

  return 0;
} /* main */

/****************************************************************************
 *
 * Private member functions
 *
 ****************************************************************************/

static void printUsage(const char* program_name)
{
  cout << "SVXLink TOTP Configuration Tool\n"
       << "Usage: " << program_name << " [OPTIONS]\n"
       << "\n"
       << "Options:\n"
       << "  -g, --generate              Generate new TOTP configuration\n"
       << "  -c, --callsign CALL         Amateur radio callsign (required for generation)\n"
       << "  -l, --logic NAME            Logic name (default: SimplexLogic)\n"
       << "  -t, --timeout SECONDS       Authentication timeout (default: 300)\n"
       << "  -m, --multi-user            Generate multi-user configuration format\n"
       << "  -a, --add-user              Add a new user (generates user config snippet)\n"
       << "  -u, --user-id ID            User ID (required for --add-user)\n"
       << "  -n, --user-name NAME        User display name (optional)\n"
       << "  -s, --secret SECRET         TOTP secret (base32 encoded)\n"
       << "  -T, --test CODE             Test a TOTP code\n"
       << "  -C, --current               Show current TOTP code\n"
       << "  -h, --help                  Show this help message\n"
       << "\n"
       << "Examples:\n"
       << "  # Generate new multi-user configuration for callsign N0CALL\n"
       << "  " << program_name << " --generate --callsign N0CALL --multi-user\n"
       << "\n"
       << "  # Add a new user\n"
       << "  " << program_name << " --add-user --user-id CR7BPM --user-name \"Carlos\"\n"
       << "\n"
       << "  # Test a TOTP code\n"
       << "  " << program_name << " --secret JBSWY3DPEHPK3PXP --test 123456\n"
       << "\n"
       << "  # Show current TOTP code\n"
       << "  " << program_name << " --secret JBSWY3DPEHPK3PXP --current\n"
       << endl;
} /* printUsage */

static void generateConfig(const string& callsign, const string& logic_name,
                          int auth_timeout, bool multi_user_format)
{
  // Generate new TOTP secret
  string secret = TOTPAuth::generateSecret();
  TOTPAuth totp(secret);
  
  cout << "=== SVXLink TOTP Configuration ===" << endl;
  cout << endl;
  
  if (multi_user_format)
  {
    cout << "Add the following sections to your svxlink.conf file:" << endl;
    cout << endl;
    
    cout << "# Enable TOTP for " << logic_name << endl;
    cout << "[" << logic_name << "]" << endl;
    cout << "TOTP_REQUIRED=1" << endl;
    cout << "TOTP_AUTH_TIMEOUT=" << auth_timeout << endl;
    cout << endl;
    
    cout << "# TOTP Authentication Parameters" << endl;
    cout << "[TOTP_AUTH]" << endl;
    cout << "TIME_WINDOW=30" << endl;
    cout << "TOTP_LENGTH=6" << endl;
    cout << "TOLERANCE_WINDOWS=1" << endl;
    cout << endl;
    
    cout << "# TOTP Users" << endl;
    cout << "[TOTP_USERS]" << endl;
    cout << callsign << "_SECRET=" << secret << endl;
    cout << callsign << "_NAME=" << callsign << endl;
    cout << endl;
  }
  else
  {
    cout << "Generated TOTP secret: " << secret << endl;
    cout << endl;
    
    cout << "Add the following lines to your svxlink.conf file in the [" 
         << logic_name << "] section:" << endl;
    cout << endl;
    cout << "# TOTP Authentication Settings (Legacy Format)" << endl;
    cout << "TOTP_REQUIRED=1" << endl;
    cout << "TOTP_AUTH_TIMEOUT=" << auth_timeout << endl;
    cout << endl;
    cout << "# Add TOTP_AUTH and TOTP_USERS sections manually" << endl;
    cout << endl;
  }
  
  // Generate provisioning URI
  string uri = totp.getProvisioningUri(callsign, "SVXLink");
  cout << "QR Code URI for smartphone apps:" << endl;
  cout << uri << endl;
  cout << endl;
  
  // Try to generate QR code if libqrencode is available
  cout << "QR Code (scan with Google Authenticator or similar app):" << endl;
  printQRCode(uri);
  cout << endl;
  
  cout << "Setup Instructions:" << endl;
  cout << "1. Add the configuration lines above to your svxlink.conf" << endl;
  cout << "2. Scan the QR code with Google Authenticator, Authy, or similar TOTP app" << endl;
  cout << "3. Restart SVXLink" << endl;
  cout << "4. Test authentication by entering a 6-digit TOTP code via RF DTMF" << endl;
  cout << "5. Once authenticated, you can use normal DTMF commands" << endl;
  cout << endl;
  
  cout << "Current TOTP code (for testing): " << totp.generateCurrentCode() << endl;
} /* generateConfig */

static void addUser(const string& user_id, const string& user_name)
{
  // Generate new TOTP secret for the user
  string secret = TOTPAuth::generateSecret();
  TOTPAuth totp(secret);
  
  cout << "=== Add TOTP User ===" << endl;
  cout << endl;
  
  cout << "Add the following lines to the [TOTP_USERS] section in your svxlink.conf:" << endl;
  cout << endl;
  cout << user_id << "_SECRET=" << secret << endl;
  cout << user_id << "_NAME=" << user_name << endl;
  cout << endl;
  
  // Generate provisioning URI
  string uri = totp.getProvisioningUri(user_name, "SVXLink");
  cout << "QR Code URI for smartphone apps:" << endl;
  cout << uri << endl;
  cout << endl;
  
  // Try to generate QR code if libqrencode is available
  cout << "QR Code (scan with Google Authenticator or similar app):" << endl;
  printQRCode(uri);
  cout << endl;
  
  cout << "Setup Instructions:" << endl;
  cout << "1. Add the configuration lines above to your svxlink.conf [TOTP_USERS] section" << endl;
  cout << "2. Scan the QR code with Google Authenticator, Authy, or similar TOTP app" << endl;
  cout << "3. Restart SVXLink" << endl;
  cout << "4. Test authentication by entering a 6-digit TOTP code via RF DTMF" << endl;
  cout << endl;
  
  cout << "Current TOTP code (for testing): " << totp.generateCurrentCode() << endl;
} /* addUser */

static void printQRCode(const string& data)
{
  QRcode* qr = QRcode_encodeString(data.c_str(), 0, QR_ECLEVEL_L, QR_MODE_8, 1);
  
  if (qr == nullptr)
  {
    cout << "Error: Could not generate QR code" << endl;
    return;
  }
  
  const char* black = "██";
  const char* white = "  ";
  
  // Add white border
  for (int i = 0; i < qr->width + 4; i++)
  {
    cout << white;
  }
  cout << endl;
  
  for (int i = 0; i < qr->width + 4; i++)
  {
    cout << white;
  }
  cout << endl;
  
  // Print QR code
  for (int y = 0; y < qr->width; y++)
  {
    cout << white << white; // Left border
    
    for (int x = 0; x < qr->width; x++)
    {
      int index = y * qr->width + x;
      if (qr->data[index] & 1)
      {
        cout << black;
      }
      else
      {
        cout << white;
      }
    }
    
    cout << white << white; // Right border
    cout << endl;
  }
  
  // Add white border
  for (int i = 0; i < qr->width + 4; i++)
  {
    cout << white;
  }
  cout << endl;
  
  for (int i = 0; i < qr->width + 4; i++)
  {
    cout << white;
  }
  cout << endl;
  
  QRcode_free(qr);
} /* printQRCode */

static void testCode(const string& secret, const string& code)
{
  TOTPAuth totp(secret);
  
  if (!totp.isConfigured())
  {
    throw runtime_error("Invalid TOTP secret");
  }
  
  bool valid = totp.validateCode(code, 1); // Use regular validation for testing
  
  cout << "TOTP Code Test Results:" << endl;
  cout << "Secret: " << secret << endl;
  cout << "Code: " << code << endl;
  cout << "Valid: " << (valid ? "YES" : "NO") << endl;
  
  if (!valid)
  {
    cout << "Current valid code: " << totp.generateCurrentCode() << endl;
  }
} /* testCode */

static void showCurrentCode(const string& secret)
{
  TOTPAuth totp(secret);
  
  if (!totp.isConfigured())
  {
    throw runtime_error("Invalid TOTP secret");
  }
  
  cout << "Current TOTP code: " << totp.generateCurrentCode() << endl;
} /* showCurrentCode */

/*
 * This file has not been truncated
 */
