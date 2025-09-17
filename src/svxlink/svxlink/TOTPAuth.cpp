/**
@file	 TOTPAuth.cpp
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

/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <random>
#include <set>
#include <map>
#include <tuple>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/rand.h>

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

static const int TOTP_DIGITS = 6;
static const int TOTP_PERIOD = 30;
static const int SECRET_LENGTH = 20; // 160 bits for strong security

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

TOTPAuth::TOTPAuth(void)
  : m_configured(false)
{
} /* TOTPAuth::TOTPAuth */

TOTPAuth::TOTPAuth(const std::string& secret_base32)
  : m_configured(false)
{
  setSecret(secret_base32);
} /* TOTPAuth::TOTPAuth */

TOTPAuth::~TOTPAuth(void)
{
  // Clear secret from memory
  if (!m_secret.empty())
  {
    memset(m_secret.data(), 0, m_secret.size());
  }
} /* TOTPAuth::~TOTPAuth */

bool TOTPAuth::setSecret(const std::string& secret_base32)
{
  try
  {
    m_secret = base32Decode(secret_base32);
    m_configured = !m_secret.empty();
    return m_configured;
  }
  catch (const exception& e)
  {
    cerr << "*** ERROR: Failed to decode TOTP secret: " << e.what() << endl;
    m_configured = false;
    return false;
  }
} /* TOTPAuth::setSecret */

std::string TOTPAuth::generateSecret(void)
{
  vector<uint8_t> secret_bytes = generateRandomBytes(SECRET_LENGTH);
  return base32Encode(secret_bytes);
} /* TOTPAuth::generateSecret */

bool TOTPAuth::validateCode(const std::string& code, int window_tolerance)
{
  if (!m_configured || code.length() != TOTP_DIGITS)
  {
    return false;
  }

  // Check if code contains only digits
  for (char c : code)
  {
    if (!isdigit(c))
    {
      return false;
    }
  }

  time_t current_time = time(nullptr);
  uint64_t current_counter = timestampToCounter(current_time);

  // Check current window and tolerance windows
  for (int i = -window_tolerance; i <= window_tolerance; ++i)
  {
    uint64_t test_counter = current_counter + i;
    string expected_code = generateCodeForCounter(test_counter);
    
    if (code == expected_code)
    {
      return true;
    }
  }

  return false;
} /* TOTPAuth::validateCode */

bool TOTPAuth::validateCodeOnce(const std::string& code, int window_tolerance)
{
  if (!m_configured || code.length() != TOTP_DIGITS)
  {
    return false;
  }

  // Check if code contains only digits
  for (char c : code)
  {
    if (!isdigit(c))
    {
      return false;
    }
  }

  time_t current_time = time(nullptr);
  uint64_t current_counter = timestampToCounter(current_time);

  // Check current window and tolerance windows
  for (int i = -window_tolerance; i <= window_tolerance; ++i)
  {
    uint64_t test_counter = current_counter + i;
    
    // Check if this counter has already been used
    if (m_used_counters.find(test_counter) != m_used_counters.end())
    {
      continue; // Skip already used counters
    }
    
    string expected_code = generateCodeForCounter(test_counter);
    
    if (code == expected_code)
    {
      // Mark this counter as used
      m_used_counters.insert(test_counter);
      
      // Clean up old counters periodically to prevent memory growth
      cleanupOldCounters();
      
      return true;
    }
  }

  return false;
} /* TOTPAuth::validateCodeOnce */

std::string TOTPAuth::generateCurrentCode(void)
{
  return generateCode(time(nullptr));
} /* TOTPAuth::generateCurrentCode */

std::string TOTPAuth::generateCode(std::time_t timestamp)
{
  if (!m_configured)
  {
    return "";
  }

  uint64_t counter = timestampToCounter(timestamp);
  return generateCodeForCounter(counter);
} /* TOTPAuth::generateCode */

std::string TOTPAuth::getProvisioningUri(const std::string& account_name,
                                         const std::string& issuer)
{
  if (!m_configured)
  {
    return "";
  }

  string secret_base32 = base32Encode(m_secret);
  
  ostringstream uri;
  uri << "otpauth://totp/" << issuer << ":" << account_name
      << "?secret=" << secret_base32
      << "&issuer=" << issuer
      << "&digits=" << TOTP_DIGITS
      << "&period=" << TOTP_PERIOD
      << "&algorithm=SHA1";

  return uri.str();
} /* TOTPAuth::getProvisioningUri */

bool TOTPAuth::isConfigured(void) const
{
  return m_configured;
} /* TOTPAuth::isConfigured */

/****************************************************************************
 *
 * Private member functions
 *
 ****************************************************************************/

std::vector<uint8_t> TOTPAuth::base32Decode(const std::string& base32_str)
{
  const string base32_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  vector<uint8_t> result;
  
  if (base32_str.empty())
  {
    return result;
  }

  string input = base32_str;
  // Remove padding
  while (!input.empty() && input.back() == '=')
  {
    input.pop_back();
  }

  // Convert to uppercase
  transform(input.begin(), input.end(), input.begin(), ::toupper);

  size_t bits = 0;
  int value = 0;

  for (char c : input)
  {
    size_t pos = base32_chars.find(c);
    if (pos == string::npos)
    {
      throw runtime_error("Invalid base32 character");
    }

    value = (value << 5) | pos;
    bits += 5;

    if (bits >= 8)
    {
      result.push_back((value >> (bits - 8)) & 0xFF);
      bits -= 8;
    }
  }

  return result;
} /* TOTPAuth::base32Decode */

std::string TOTPAuth::base32Encode(const std::vector<uint8_t>& data)
{
  const string base32_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  string result;
  
  if (data.empty())
  {
    return result;
  }

  int bits = 0;
  int value = 0;

  for (uint8_t byte : data)
  {
    value = (value << 8) | byte;
    bits += 8;

    while (bits >= 5)
    {
      result += base32_chars[(value >> (bits - 5)) & 0x1F];
      bits -= 5;
    }
  }

  if (bits > 0)
  {
    result += base32_chars[(value << (5 - bits)) & 0x1F];
  }

  // Add padding
  while (result.length() % 8 != 0)
  {
    result += '=';
  }

  return result;
} /* TOTPAuth::base32Encode */

std::vector<uint8_t> TOTPAuth::hmacSha1(const std::vector<uint8_t>& key,
                                        const std::vector<uint8_t>& data)
{
  vector<uint8_t> result(SHA_DIGEST_LENGTH);
  unsigned int len = 0;

  HMAC(EVP_sha1(), key.data(), key.size(), data.data(), data.size(),
       result.data(), &len);

  result.resize(len);
  return result;
} /* TOTPAuth::hmacSha1 */

uint64_t TOTPAuth::timestampToCounter(std::time_t timestamp)
{
  return static_cast<uint64_t>(timestamp) / TOTP_PERIOD;
} /* TOTPAuth::timestampToCounter */

std::string TOTPAuth::generateCodeForCounter(uint64_t counter)
{
  // Convert counter to big-endian bytes
  vector<uint8_t> counter_bytes(8);
  for (int i = 7; i >= 0; --i)
  {
    counter_bytes[i] = counter & 0xFF;
    counter >>= 8;
  }

  // Generate HMAC-SHA1
  vector<uint8_t> hmac_result = hmacSha1(m_secret, counter_bytes);

  // Dynamic truncation
  int offset = hmac_result.back() & 0x0F;
  uint32_t code = ((hmac_result[offset] & 0x7F) << 24) |
                  ((hmac_result[offset + 1] & 0xFF) << 16) |
                  ((hmac_result[offset + 2] & 0xFF) << 8) |
                  (hmac_result[offset + 3] & 0xFF);

  // Generate 6-digit code
  code %= 1000000;

  ostringstream oss;
  oss << setfill('0') << setw(TOTP_DIGITS) << code;
  return oss.str();
} /* TOTPAuth::generateCodeForCounter */

std::vector<uint8_t> TOTPAuth::generateRandomBytes(size_t length)
{
  vector<uint8_t> result(length);
  
  if (RAND_bytes(result.data(), length) != 1)
  {
    // Fallback to C++ random if OpenSSL fails
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(0, 255);
    
    for (size_t i = 0; i < length; ++i)
    {
      result[i] = dis(gen);
    }
  }

  return result;
} /* TOTPAuth::generateRandomBytes */

void TOTPAuth::cleanupOldCounters(void)
{
  // Only cleanup if we have too many stored counters
  if (m_used_counters.size() < 100)
  {
    return;
  }
  
  time_t current_time = time(nullptr);
  uint64_t current_counter = timestampToCounter(current_time);
  
  // Remove counters older than 10 minutes (20 time windows)
  // This ensures we keep recent counters to prevent replay attacks
  // while preventing unlimited memory growth
  uint64_t cleanup_threshold = current_counter - 20;
  
  auto it = m_used_counters.begin();
  while (it != m_used_counters.end())
  {
    if (*it < cleanup_threshold)
    {
      it = m_used_counters.erase(it);
    }
    else
    {
      ++it;
    }
  }
} /* TOTPAuth::cleanupOldCounters */

/****************************************************************************
 *
 * TOTPValidator implementation
 *
 ****************************************************************************/

TOTPValidator::TOTPValidator(void)
  : m_enabled(false), m_authenticated(false), m_collecting_totp(false),
    m_auth_timestamp(0), m_auth_timeout(300), m_time_window(30), 
    m_totp_length(6), m_tolerance_windows(1)
{
} /* TOTPValidator::TOTPValidator */

TOTPValidator::~TOTPValidator(void)
{
  clearBuffer();
} /* TOTPValidator::~TOTPValidator */

bool TOTPValidator::initialize(int time_window, int totp_length, 
                              int tolerance_windows, int auth_timeout)
{
  m_time_window = time_window;
  m_totp_length = totp_length;
  m_tolerance_windows = tolerance_windows;
  m_auth_timeout = auth_timeout;
  
  // TOTP is enabled if we have any users configured
  m_enabled = !m_users.empty();
  
  if (m_enabled)
  {
    cout << "TOTP authentication enabled with " << m_users.size() 
         << " user(s), " << m_totp_length << "-digit codes, " 
         << m_time_window << "s window, ±" << m_tolerance_windows 
         << " tolerance" << endl;
  }
  
  return true;
} /* TOTPValidator::initialize */

bool TOTPValidator::addUser(const std::string& user_id, const std::string& user_name,
                           const std::string& secret_base32)
{
  if (secret_base32.empty())
  {
    cerr << "*** ERROR: Empty secret for user " << user_id << endl;
    return false;
  }
  
  try
  {
    // Create TOTPAuth first to check if it's valid
    TOTPAuth test_auth(secret_base32);
    if (!test_auth.isConfigured())
    {
      cerr << "*** ERROR: Invalid TOTP secret for user " << user_id << endl;
      return false;
    }
    
    // Use emplace to construct UserInfo in-place
    m_users.emplace(std::piecewise_construct,
                    std::forward_as_tuple(user_id),
                    std::forward_as_tuple(user_name, secret_base32));
    cout << "Added TOTP user: " << user_id << " (" << user_name << ")" << endl;
    return true;
  }
  catch (const exception& e)
  {
    cerr << "*** ERROR: Failed to add TOTP user " << user_id << ": " << e.what() << endl;
    return false;
  }
} /* TOTPValidator::addUser */

bool TOTPValidator::processDtmfDigit(char digit)
{
  if (!m_enabled)
  {
    return false; // TOTP not enabled, don't consume digit
  }

  // Check if authentication has expired
  if (m_authenticated && hasAuthenticationExpired())
  {
    resetAuthentication();
  }

  // If already authenticated, don't consume digits
  if (m_authenticated)
  {
    return false;
  }

  // Start collecting TOTP if we receive a digit
  if (!m_collecting_totp)
  {
    m_collecting_totp = true;
    clearBuffer();
  }

  // Only accept numeric digits for TOTP
  if (digit >= '0' && digit <= '9')
  {
    m_totp_buffer += digit;
    
    // Check if we have a complete TOTP code
    if (m_totp_buffer.length() >= m_totp_length)
    {
      processCompletedCode();
      return true; // Still consumed the digit
    }
    
    return true; // Digit consumed for TOTP
  }
  else if (digit == '*')
  {
    // * resets TOTP input
    clearBuffer();
    return true; // Digit consumed
  }
  else if (digit == '#')
  {
    // # processes current buffer even if not complete
    if (!m_totp_buffer.empty())
    {
      processCompletedCode();
    }
    return true; // Digit consumed
  }

  return false; // Digit not consumed
} /* TOTPValidator::processDtmfDigit */

bool TOTPValidator::isAuthenticated(void) const
{
  if (!m_enabled)
  {
    return true; // If TOTP is disabled, consider always authenticated
  }

  return m_authenticated && !hasAuthenticationExpired();
} /* TOTPValidator::isAuthenticated */

bool TOTPValidator::isEnabled(void) const
{
  return m_enabled;
} /* TOTPValidator::isEnabled */

void TOTPValidator::resetAuthentication(void)
{
  m_authenticated = false;
  m_auth_timestamp = 0;
  m_authenticated_user.clear();
  clearBuffer();
} /* TOTPValidator::resetAuthentication */

std::vector<std::string> TOTPValidator::getUserList(void) const
{
  vector<string> user_list;
  for (const auto& user_pair : m_users)
  {
    user_list.push_back(user_pair.first);
  }
  return user_list;
} /* TOTPValidator::getUserList */

/****************************************************************************
 *
 * Private member functions for TOTPValidator
 *
 ****************************************************************************/

void TOTPValidator::processCompletedCode(void)
{
  if (m_totp_buffer.empty())
  {
    clearBuffer();
    return;
  }

  // Pad with zeros if needed (for shorter codes)
  while (m_totp_buffer.length() < m_totp_length)
  {
    m_totp_buffer = "0" + m_totp_buffer;
  }

  // Try to validate the code against all users
  for (auto& user_pair : m_users)
  {
    const string& user_id = user_pair.first;
    UserInfo& user_info = user_pair.second;
    
    bool valid = user_info.totp_auth.validateCodeOnce(m_totp_buffer, m_tolerance_windows);
    
    if (valid)
    {
      m_authenticated = true;
      m_auth_timestamp = time(nullptr);
      m_authenticated_user = user_id;
      cout << "TOTP authentication successful for user: " << user_info.name 
           << " (" << user_id << ")" << endl;
      clearBuffer();
      return;
    }
  }
  
  cout << "TOTP authentication failed - code not valid for any user" << endl;
  clearBuffer();
} /* TOTPValidator::processCompletedCode */

void TOTPValidator::clearBuffer(void)
{
  m_totp_buffer.clear();
  m_collecting_totp = false;
} /* TOTPValidator::clearBuffer */

bool TOTPValidator::hasAuthenticationExpired(void) const
{
  if (!m_authenticated)
  {
    return true;
  }

  time_t current_time = time(nullptr);
  return (current_time - m_auth_timestamp) > m_auth_timeout;
} /* TOTPValidator::hasAuthenticationExpired */

/*
 * This file has not been truncated
 */
