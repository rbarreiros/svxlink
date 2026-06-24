/**
@file    SvxPlayer.h
@brief   Reflector client that plays audio files to a talk group
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

#ifndef SVX_PLAYER_INCLUDED
#define SVX_PLAYER_INCLUDED


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <sys/time.h>
#include <string>
#include <queue>
#include <vector>
#include <cstdint>

#include <sigc++/sigc++.h>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncConfig.h>
#include <AsyncTimer.h>
#include <AsyncAudioEncoder.h>
#include <AsyncAudioDecoder.h>
#include <AsyncAudioPacer.h>
#include <AsyncPty.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "../reflectorclient/ReflectorClient.h"
#include "Scheduler.h"
#include "MsgHandler.h"


/****************************************************************************
 *
 * Forward declarations
 *
 ****************************************************************************/


/****************************************************************************
 *
 * Class definitions
 *
 ****************************************************************************/

/**
@brief  Reflector client that plays audio files to a TG
@author Rodrigo Barreiros
@date   2026-02-27

SvxPlayer connects to an SvxReflector, authenticates with full PKI/SSL just
like ReflectorLogic, selects a talk group, and transmits audio from WAV/GSM/
raw PCM files.  Playback can be triggered by a cron-like schedule or on demand
through a PTY interface.
*/
class SvxPlayer : public ReflectorClient
{
  public:
    /**
     * @brief  Constructor
     */
    SvxPlayer(void);

    /**
     * @brief  Destructor
     */
    ~SvxPlayer(void) override;

    /**
     * @brief  Initialize the player from configuration
     * @param  cfg   Previously opened Async::Config object
     * @return true on success
     */
    bool initialize(Async::Config& cfg, const std::string& section);

    /**
     * @brief  Queue a file for playback on the given talk group
     * @param  file  Path to the audio file
     * @param  tg    Talk group (0 = use DEFAULT_TG)
     */
    void playFile(const std::string& file, uint32_t tg = 0);

    /**
     * @brief  Queue multiple files for sequential playback on the given talk group
     * @param  files   Ordered list of audio file paths
     * @param  tg      Talk group (0 = use DEFAULT_TG)
     * @param  gap_ms  Milliseconds of silence (PTT released) inserted between
     *                 consecutive files; 0 means back-to-back playback
     */
    void playFiles(const std::vector<std::string>& files,
                   uint32_t tg = 0, uint32_t gap_ms = 0);

    /**
     * @brief  Queue a CW message for playback
     * @param  wpm     Words per minute
     * @param  pitch   Pitch in Hz
     * @param  msg     The message to play
     * @param  tg      Talk group (0 = use DEFAULT_TG)
     */
    void playCw(int wpm, int pitch, const std::string& msg, uint32_t tg = 0);

    /**
     * @brief  Abort current playback and clear the playback queue
     */
    void stop(void);

  protected:
    void onConnected(void) override;
    void onDisconnected(void) override;
    void onLoggedIn(void) override;
    void onTalkerStart(uint32_t tg, const std::string& callsign) override;
    void onTalkerStop(uint32_t tg, const std::string& callsign) override;
    void onAllSamplesFlushed(void) override;
    void onCodecNegotiated(const std::string& codec) override;

  private:
    struct PlayRequest
    {
      std::string file;
      std::string cw_msg;
      int         cw_wpm = 0;
      int         cw_pitch = 0;
      uint32_t    tg = 0;
      uint32_t    gap_before_ms = 0;
    };

        

    

    
    
    
    

    Async::Config*                    m_cfg                = nullptr;
    std::string                       m_name               {"SvxPlayer"};
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    

    Async::AudioEncoder*              m_enc                = nullptr;
    Async::AudioDecoder*              m_dec                = nullptr;
    Async::AudioPacer*                m_pacer              = nullptr;
    MsgHandler*                       m_msg_handler        = nullptr;

    uint32_t                          m_default_tg         = 0;
    uint32_t                          m_cw_preamble_ms     = 500;
    uint32_t                          m_cw_postamble_ms    = 500;

    std::queue<PlayRequest>           m_play_queue;
    bool                              m_playing            = false;
    Async::Timer                      m_gap_timer;

    Async::Pty*                       m_pty                = nullptr;
    std::string                       m_pty_path;

    Scheduler                         m_scheduler;

    
    
    
    
    
    
    
    
    
    

    
    
    

    SvxPlayer(const SvxPlayer&);
    SvxPlayer& operator=(const SvxPlayer&);

    
    
    bool setupScheduler(void);
    bool setupPty(void);

    
    
    
    
    

    
    
    
    
    
    
    
    
    
    
    

    
    
    
    
    
    
    
    

    
    
    
    
    

    bool setAudioCodec(const std::string& codec_name);
    bool codecIsAvailable(const std::string& codec_name);

    
    
    void allMsgsWritten(void);
    

    
    void startNextPlayback(void);
    void onGapTimerExpired(Async::Timer*);

    

    void onPtyData(const void* buf, size_t len);
    void processCommand(const std::string& line);
};


#endif /* SVX_PLAYER_INCLUDED */

/*
 * This file has not been truncated
 */
