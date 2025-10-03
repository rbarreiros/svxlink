/**
@file	 SquelchPttId.h
@brief   A PTT ID squelch detector that validates DTMF user IDs
@author  Rui Barreiros / CR7BPM
@date	 2024-12-19

\verbatim
SvxLink - A Multi Purpose Voice Services System for Ham Radio Use
Copyright (C) 2003-2022 Tobias Blomberg / SM0SVX

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

#ifndef SQUELCH_PTTID_INCLUDED
#define SQUELCH_PTTID_INCLUDED


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <string>
#include <map>
#include <memory>
#include <chrono>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncConfig.h>
#include <AsyncTimer.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "Squelch.h"
#include "DtmfDecoder.h"


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

//namespace MyNameSpace
//{


/****************************************************************************
 *
 * Forward declarations of classes inside of the declared namespace
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
@brief  A PTT ID squelch detector that validates DTMF user IDs
@author Rui Barreiros / CR7BPM
@date   2024-12-19

This squelch detector listens for DTMF digits and validates them against
a configured user database. It can be used with SQL_COMBINE to create
access control for RF communications.

Configuration example:
[Rx1:PTTID]
SQL_DET=PTTID
PTTID_REQUIRED=1
PTTID_TIMEOUT=5000

[PTTID_USERS]
123456=CR7BPM
789012=SM0ABC

[PTTID_ACCOUNTS]
CR7BPM_NAME=Rui Barreiros
CR7BPM_ACTIVE=1
CR7BPM_BLOCKED=0
SM0ABC_NAME=John Doe
SM0ABC_ACTIVE=1
SM0ABC_BLOCKED=30
*/
class SquelchPttId : public Squelch
{
  public:
      /// The name of this class when used by the object factory
    static constexpr const char* OBJNAME = "PTTID";

    /**
     * @brief 	Default constructor
     */
    explicit SquelchPttId(void);

    /**
     * @brief 	Destructor
     */
    virtual ~SquelchPttId(void);

    /**
     * @brief 	Initialize the PTT ID squelch detector
     * @param 	cfg A previously initialized config object
     * @param   rx_name The name of the RX (config section name)
     * @return	Returns \em true on success or else \em false
     */
    virtual bool initialize(Async::Config& cfg, const std::string& rx_name);

    /**
     * @brief 	Reset the squelch detector
     *
     * Reset the squelch so that the detection process starts from
     * the beginning again.
     */
    virtual void reset(void);

    /**
     * @brief 	Restart the squelch detector
     *
     * Restart the squelch detector after a timeout or other condition.
     */
    virtual void restart(void);

  protected:
    /**
     * @brief 	Process the incoming samples in the squelch detector
     * @param 	samples A buffer containing samples
     * @param 	count The number of samples in the buffer
     * @return	Return the number of processed samples
     */
    int processSamples(const float *samples, int count);

  private:
    struct UserAccount
    {
      std::string name;
      bool active;
      int blocked_minutes;
      std::chrono::steady_clock::time_point blocked_until;
      
      UserAccount() : active(true), blocked_minutes(0) {}
    };

    typedef std::map<std::string, std::string> UserMap;  // DTMF ID -> username
    typedef std::map<std::string, UserAccount> AccountMap;  // username -> account

    UserMap m_users;
    AccountMap m_accounts;
    std::unique_ptr<DtmfDecoder> m_dtmf_decoder;
    std::string m_current_dtmf_id;
    bool m_pttid_required;
    int m_pttid_timeout_ms;
    std::unique_ptr<Async::Timer> m_timeout_timer;
    bool m_valid_user_detected;
    std::string m_detected_username;

    SquelchPttId(const SquelchPttId&);
    SquelchPttId& operator=(const SquelchPttId&);

    void onDtmfDigitActivated(char digit);
    void onDtmfDigitDeactivated(char digit, int duration);
    void onTimeout(Async::Timer *timer);
    void processDtmfId(const std::string& dtmf_id);
    bool isUserValid(const std::string& username);
    void updateBlockedUsers();
    void loadUserConfiguration(Async::Config& cfg);
    void loadAccountConfiguration(Async::Config& cfg);
    void startTimeoutTimer();
    void stopTimeoutTimer();
};  /* class SquelchPttId */


//} /* namespace */

#endif /* SQUELCH_PTTID_INCLUDED */



/*
 * This file has not been truncated
 */
