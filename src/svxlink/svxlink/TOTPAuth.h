/**
@file	 TOTPAuth.h
@brief   TOTP (Time-based One-Time Password) authentication for SVXLink
@author  Generated for SVXLink TOTP Implementation
@date	 2025-09-17

This class implements TOTP authentication compatible with Google Authenticator
and other RFC 6238 compliant applications. It provides secure authentication
for RF DTMF commands before they are processed by the logic core.

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

#ifndef TOTP_AUTH_INCLUDED
#define TOTP_AUTH_INCLUDED

/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <string>
#include <vector>
#include <set>
#include <map>
#include <cstdint>
#include <ctime>

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
@brief	TOTP authentication implementation compatible with Google Authenticator
@author Generated for SVXLink TOTP Implementation
@date   2025-09-17

This class implements Time-based One-Time Password (TOTP) authentication as
defined in RFC 6238. It's compatible with Google Authenticator, Authy, and
other TOTP applications. The class provides methods to generate and validate
TOTP codes using a shared secret.

Key features:
- RFC 6238 compliant TOTP implementation
- 6-digit codes (standard)
- 30-second time window (standard)
- Base32 secret encoding for QR code compatibility
- Time window tolerance for clock drift
- Secure secret generation and validation
*/
class TOTPAuth
{
  public:
    /**
     * @brief 	Default constructor
     */
    TOTPAuth(void);

    /**
     * @brief 	Constructor with secret
     * @param 	secret_base32 The TOTP secret in base32 format
     */
    TOTPAuth(const std::string& secret_base32);

    /**
     * @brief 	Destructor
     */
    ~TOTPAuth(void);

    /**
     * @brief   Set the TOTP secret
     * @param   secret_base32 The secret in base32 format
     * @returns Returns true on success, false on invalid secret
     */
    bool setSecret(const std::string& secret_base32);

    /**
     * @brief   Generate a random TOTP secret
     * @returns Returns the generated secret in base32 format
     */
    static std::string generateSecret(void);

    /**
     * @brief   Validate a TOTP code
     * @param   code The 6-digit TOTP code to validate
     * @param   window_tolerance Number of time windows to check (default: 1)
     * @returns Returns true if code is valid, false otherwise
     */
    bool validateCode(const std::string& code, int window_tolerance = 1);

    /**
     * @brief   Validate a TOTP code with replay protection
     * @param   code The 6-digit TOTP code to validate
     * @param   window_tolerance Number of time windows to check (default: 1)
     * @returns Returns true if code is valid and not previously used
     */
    bool validateCodeOnce(const std::string& code, int window_tolerance = 1);

    /**
     * @brief   Generate current TOTP code
     * @returns Returns the current 6-digit TOTP code
     */
    std::string generateCurrentCode(void);

    /**
     * @brief   Generate TOTP code for specific timestamp
     * @param   timestamp Unix timestamp to generate code for
     * @returns Returns the 6-digit TOTP code for the timestamp
     */
    std::string generateCode(std::time_t timestamp);

    /**
     * @brief   Get the provisioning URI for QR code generation
     * @param   account_name The account name (e.g., callsign)
     * @param   issuer The issuer name (e.g., "SVXLink")
     * @returns Returns the otpauth:// URI for QR code generation
     */
    std::string getProvisioningUri(const std::string& account_name,
                                   const std::string& issuer = "SVXLink");

    /**
     * @brief   Check if secret is set and valid
     * @returns Returns true if secret is configured, false otherwise
     */
    bool isConfigured(void) const;

  private:
    std::vector<uint8_t> m_secret;
    bool m_configured;
    std::set<uint64_t> m_used_counters; // Track used time counters to prevent replay

    /**
     * @brief   Decode base32 string to binary data
     * @param   base32_str The base32 encoded string
     * @returns Returns the decoded binary data
     */
    static std::vector<uint8_t> base32Decode(const std::string& base32_str);

    /**
     * @brief   Encode binary data to base32 string
     * @param   data The binary data to encode
     * @returns Returns the base32 encoded string
     */
    static std::string base32Encode(const std::vector<uint8_t>& data);

    /**
     * @brief   Generate HMAC-SHA1 hash
     * @param   key The key for HMAC
     * @param   data The data to hash
     * @returns Returns the HMAC-SHA1 hash
     */
    static std::vector<uint8_t> hmacSha1(const std::vector<uint8_t>& key,
                                         const std::vector<uint8_t>& data);

    /**
     * @brief   Convert timestamp to time counter
     * @param   timestamp Unix timestamp
     * @returns Returns the time counter (timestamp / 30)
     */
    static uint64_t timestampToCounter(std::time_t timestamp);

    /**
     * @brief   Generate TOTP code for time counter
     * @param   counter The time counter
     * @returns Returns the 6-digit TOTP code
     */
    std::string generateCodeForCounter(uint64_t counter);

    /**
     * @brief   Generate secure random bytes
     * @param   length Number of bytes to generate
     * @returns Returns vector of random bytes
     */
    static std::vector<uint8_t> generateRandomBytes(size_t length);

    /**
     * @brief   Clean up old used counters to prevent memory growth
     */
    void cleanupOldCounters(void);

    // Prevent copying
    TOTPAuth(const TOTPAuth&);
    TOTPAuth& operator=(const TOTPAuth&);
};

/**
@brief	Multi-user TOTP validator for SVXLink integration
@author Generated for SVXLink TOTP Implementation  
@date   2025-09-17

This class provides integration between TOTP authentication and SVXLink's
DTMF processing system. It manages TOTP validation state for multiple users
and provides methods to check if DTMF commands should be processed based on 
TOTP authentication status.
*/
class TOTPValidator
{
  public:
    struct UserInfo
    {
      std::string name;
      std::string secret;
      TOTPAuth totp_auth;
      
      UserInfo(const std::string& user_name, const std::string& user_secret)
        : name(user_name), secret(user_secret), totp_auth(user_secret) {}
    };

    /**
     * @brief 	Default constructor
     */
    TOTPValidator(void);

    /**
     * @brief 	Destructor
     */
    ~TOTPValidator(void);

    /**
     * @brief   Initialize the TOTP validator with configuration parameters
     * @param   time_window Time window in seconds (default: 30)
     * @param   totp_length Length of TOTP codes (default: 6)
     * @param   tolerance_windows Number of tolerance windows (default: 1)
     * @param   auth_timeout Authentication timeout in seconds (default: 300)
     * @returns Returns true on success, false on failure
     */
    bool initialize(int time_window = 30, int totp_length = 6, 
                   int tolerance_windows = 1, int auth_timeout = 300);

    /**
     * @brief   Add a user to the TOTP system
     * @param   user_id User identifier (e.g., callsign or USER1)
     * @param   user_name Display name for the user
     * @param   secret_base32 The TOTP secret in base32 format
     * @returns Returns true on success, false on failure
     */
    bool addUser(const std::string& user_id, const std::string& user_name,
                 const std::string& secret_base32);

    /**
     * @brief   Process a DTMF digit for TOTP authentication
     * @param   digit The DTMF digit received
     * @returns Returns true if digit was consumed for auth, false otherwise
     */
    bool processDtmfDigit(char digit);

    /**
     * @brief   Check if user is currently authenticated
     * @returns Returns true if authenticated, false otherwise
     */
    bool isAuthenticated(void) const;

    /**
     * @brief   Check if TOTP authentication is enabled
     * @returns Returns true if TOTP is configured and enabled
     */
    bool isEnabled(void) const;

    /**
     * @brief   Reset authentication state
     */
    void resetAuthentication(void);

    /**
     * @brief   Get current TOTP input buffer
     * @returns Returns the current TOTP code being entered
     */
    std::string getCurrentInput(void) const { return m_totp_buffer; }

    /**
     * @brief   Check if currently collecting TOTP input
     * @returns Returns true if expecting TOTP input, false otherwise
     */
    bool isCollectingInput(void) const { return m_collecting_totp; }

    /**
     * @brief   Get the name of the currently authenticated user
     * @returns Returns the authenticated user's name, empty if not authenticated
     */
    std::string getAuthenticatedUser(void) const { return m_authenticated_user; }

    /**
     * @brief   Get list of configured users
     * @returns Returns vector of user IDs
     */
    std::vector<std::string> getUserList(void) const;

  private:
    std::map<std::string, UserInfo> m_users;
    bool m_enabled;
    bool m_authenticated;
    bool m_collecting_totp;
    std::string m_totp_buffer;
    std::time_t m_auth_timestamp;
    int m_auth_timeout;
    int m_time_window;
    int m_totp_length;
    int m_tolerance_windows;
    std::string m_authenticated_user;

    /**
     * @brief   Process completed TOTP code
     */
    void processCompletedCode(void);

    /**
     * @brief   Clear TOTP input buffer
     */
    void clearBuffer(void);

    /**
     * @brief   Check if authentication has expired
     * @returns Returns true if auth expired, false otherwise
     */
    bool hasAuthenticationExpired(void) const;

    // Prevent copying
    TOTPValidator(const TOTPValidator&);
    TOTPValidator& operator=(const TOTPValidator&);
};

#endif /* TOTP_AUTH_INCLUDED */

/*
 * This file has not been truncated
 */

