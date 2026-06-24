/**
@file    SvxPlayer.cpp
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


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <unistd.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#include <sstream>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <iterator>
#include <streambuf>
#include <numeric>
#include <cassert>
#include <vector>
#include <cctype>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <json/json.h>

#include <AsyncApplication.h>
#include <AsyncAudioPassthrough.h>
#include <version/SVXLINK.h>
#include <config.h>


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
 * Local variables
 *
 ****************************************************************************/

namespace {

const char* getMorse(char c)
{
  switch (std::toupper(static_cast<unsigned char>(c)))
  {
    case 'A': return ".-";
    case 'B': return "-...";
    case 'C': return "-.-.";
    case 'D': return "-..";
    case 'E': return ".";
    case 'F': return "..-.";
    case 'G': return "--.";
    case 'H': return "....";
    case 'I': return "..";
    case 'J': return ".---";
    case 'K': return "-.-";
    case 'L': return ".-..";
    case 'M': return "--";
    case 'N': return "-.";
    case 'O': return "---";
    case 'P': return ".--.";
    case 'Q': return "--.-";
    case 'R': return ".-.";
    case 'S': return "...";
    case 'T': return "-";
    case 'U': return "..-";
    case 'V': return "...-";
    case 'W': return ".--";
    case 'X': return "-..-";
    case 'Y': return "-.--";
    case 'Z': return "--..";
    case '0': return "-----";
    case '1': return ".----";
    case '2': return "..---";
    case '3': return "...--";
    case '4': return "....-";
    case '5': return ".....";
    case '6': return "-....";
    case '7': return "--...";
    case '8': return "---..";
    case '9': return "----.";
    case '.': return ".-.-.-";
    case ',': return "--..--";
    case '?': return "..--..";
    case '/': return "-..-.";
    case '=': return "-...-";
    default: return "";
  }
}

} // namespace


/****************************************************************************
 *
 * Public member functions
 *
 ****************************************************************************/

SvxPlayer::SvxPlayer(void)
  : ReflectorClient(),
    m_gap_timer(0, Async::Timer::TYPE_ONESHOT, false)
{
  m_gap_timer.expired.connect(
      sigc::mem_fun(*this, &SvxPlayer::onGapTimerExpired));
} /* SvxPlayer::SvxPlayer */


SvxPlayer::~SvxPlayer(void)
{
  delete m_pty;
  m_pty = nullptr;
  if (m_msg_handler != nullptr)
  {
    m_msg_handler->unregisterSink();
  }
  delete m_msg_handler;
  m_msg_handler = nullptr;
  if (m_pacer != nullptr)
  {
    m_pacer->unregisterSink();
  }
  delete m_pacer;
  m_pacer = nullptr;
  delete m_enc;
  m_enc = nullptr;
  delete m_dec;
  m_dec = nullptr;
} /* SvxPlayer::~SvxPlayer */


bool SvxPlayer::initialize(Async::Config& cfg, const std::string& section)
{
  m_cfg = &cfg;
  m_name = section;

  if (!ReflectorClient::initialize(cfg, section))
  {
    return false;
  }

  cfg.getValue(m_name, "DEFAULT_TG", m_default_tg);
  cfg.getValue(m_name, "CW_PREAMBLE_MS", m_cw_preamble_ms);
  cfg.getValue(m_name, "CW_POSTAMBLE_MS", m_cw_postamble_ms);

  m_msg_handler = new MsgHandler(INTERNAL_SAMPLE_RATE);
  m_msg_handler->allMsgsWritten.connect(
      sigc::mem_fun(*this, &SvxPlayer::allMsgsWritten));

  m_pacer = new Async::AudioPacer(INTERNAL_SAMPLE_RATE, 320, 0);
  m_msg_handler->registerSink(m_pacer, false);

  if (!setAudioCodec("DUMMY"))
  {
    return false;
  }

  if (!setupScheduler())
  {
    return false;
  }

  if (!setupPty())
  {
    return false;
  }
  
  return true;
} /* SvxPlayer::initialize */


void SvxPlayer::playFile(const string& file, uint32_t tg)
{
  playFiles({file}, tg, 0);
} /* SvxPlayer::playFile */


void SvxPlayer::playFiles(const vector<string>& files, uint32_t tg,
                           uint32_t gap_ms)
{
  if (files.empty())
  {
    return;
  }
  if (tg == 0)
  {
    tg = m_default_tg;
  }
  for (size_t i = 0; i < files.size(); ++i)
  {
    PlayRequest req;
    req.file          = files[i];
    req.tg            = tg;
    req.gap_before_ms = (i == 0) ? 0 : gap_ms;
    m_play_queue.push(req);
  }
  if (ReflectorClient::isLoggedIn() && !m_playing)
  {
    startNextPlayback();
  }
} /* SvxPlayer::playFiles */


void SvxPlayer::playCw(int wpm, int pitch, const string& msg, uint32_t tg)
{
  if (msg.empty())
  {
    return;
  }
  if (tg == 0)
  {
    tg = m_default_tg;
  }
  PlayRequest req;
  req.cw_msg        = msg;
  req.cw_wpm        = wpm;
  req.cw_pitch      = pitch;
  req.tg            = tg;
  req.gap_before_ms = 0;
  m_play_queue.push(req);

  if (ReflectorClient::isLoggedIn() && !m_playing)
  {
    startNextPlayback();
  }
} /* SvxPlayer::playCw */


void SvxPlayer::stop(void)
{
  m_gap_timer.setEnable(false);
  while (!m_play_queue.empty())
  {
    m_play_queue.pop();
  }
  if (m_playing)
  {
    m_playing = false;
    m_msg_handler->clear();
  }
} /* SvxPlayer::stop */


/****************************************************************************
 *
 * Private member functions
 *
 ****************************************************************************/


bool SvxPlayer::setupScheduler(void)
{
  m_scheduler.playFiles.connect(
      sigc::mem_fun(*this, &SvxPlayer::playFiles));
  return m_scheduler.initialize(*m_cfg, m_name, m_default_tg);
} /* SvxPlayer::setupScheduler */


bool SvxPlayer::setupPty(void)
{
  m_cfg->getValue(m_name, "PTY", m_pty_path);
  if (m_pty_path.empty())
  {
    return true;
  }

  m_pty = new Async::Pty(m_pty_path);
  if (!m_pty->open())
  {
    cerr << "*** ERROR[" << m_name << "]: Failed to open PTY at '"
         << m_pty_path << "'" << endl;
    delete m_pty;
    m_pty = nullptr;
    return false;
  }

  m_pty->dataReceived.connect(
      sigc::mem_fun(*this, &SvxPlayer::onPtyData));

  cout << m_name << ": PTY opened at '" << m_pty_path << "'" << endl;
  return true;
} /* SvxPlayer::setupPty */





bool SvxPlayer::setAudioCodec(const string& codec_name)
{
  if (m_pacer != nullptr)
  {
    m_pacer->unregisterSink();
  }
  delete m_enc;
  m_enc = Async::AudioEncoder::create(codec_name);
  if (m_enc == nullptr)
  {
    cerr << "*** ERROR[" << m_name << "]: Failed to initialize "
         << codec_name << " audio encoder" << endl;
    m_enc = Async::AudioEncoder::create("DUMMY");
    assert(m_enc != nullptr);
    return false;
  }
  m_enc->writeEncodedSamples.connect(
      sigc::mem_fun(*this, &SvxPlayer::sendEncodedAudio));
  m_enc->flushEncodedSamples.connect(
      sigc::mem_fun(*this, &SvxPlayer::flushEncodedAudio));

  if (m_pacer != nullptr)
  {
    m_pacer->registerSink(m_enc, false);
  }

  string opt_prefix(m_enc->name());
  opt_prefix += "_ENC_";
  list<string> names = m_cfg->listSection(m_name);
  for (const auto& n : names)
  {
    if (n.find(opt_prefix) == 0)
    {
      string opt_value;
      m_cfg->getValue(m_name, n, opt_value);
      string opt_name(n.substr(opt_prefix.size()));
      m_enc->setOption(opt_name, opt_value);
    }
  }
  m_enc->printCodecParams();

  Async::AudioSink* sink = nullptr;
  if (m_dec != nullptr)
  {
    sink = m_dec->sink();
    m_dec->unregisterSink();
    delete m_dec;
  }
  m_dec = Async::AudioDecoder::create(codec_name);
  if (m_dec == nullptr)
  {
    cerr << "*** ERROR[" << m_name << "]: Failed to initialize "
         << codec_name << " audio decoder" << endl;
    m_dec = Async::AudioDecoder::create("DUMMY");
    assert(m_dec != nullptr);
    return false;
  }
  m_dec->allEncodedSamplesFlushed.connect(
      sigc::mem_fun(*this, &SvxPlayer::onAllSamplesFlushed));
  if (sink != nullptr)
  {
    m_dec->registerSink(sink, true);
  }

  opt_prefix = string(m_dec->name()) + "_DEC_";
  names = m_cfg->listSection(m_name);
  for (const auto& n : names)
  {
    if (n.find(opt_prefix) == 0)
    {
      string opt_value;
      m_cfg->getValue(m_name, n, opt_value);
      string opt_name(n.substr(opt_prefix.size()));
      m_dec->setOption(opt_name, opt_value);
    }
  }
  m_dec->printCodecParams();

  return true;
} /* SvxPlayer::setAudioCodec */


bool SvxPlayer::codecIsAvailable(const string& codec_name)
{
  return Async::AudioEncoder::isAvailable(codec_name) &&
         Async::AudioDecoder::isAvailable(codec_name);
} /* SvxPlayer::codecIsAvailable */


void SvxPlayer::onConnected(void)
{
  ReflectorClient::onConnected();
} /* SvxPlayer::onConnected */

void SvxPlayer::onDisconnected(void)
{
  ReflectorClient::onDisconnected();
  m_playing = false;
  m_gap_timer.setEnable(false);
  m_msg_handler->clear();
  while (!m_play_queue.empty())
  {
    m_play_queue.pop();
  }
} /* SvxPlayer::onDisconnected */

void SvxPlayer::onLoggedIn(void)
{
  ReflectorClient::onLoggedIn();
  if (!m_play_queue.empty() && !m_playing)
  {
    startNextPlayback();
  }
} /* SvxPlayer::onLoggedIn */

void SvxPlayer::onTalkerStart(uint32_t tg, const std::string& callsign)
{
} /* SvxPlayer::onTalkerStart */

void SvxPlayer::onTalkerStop(uint32_t tg, const std::string& callsign)
{
} /* SvxPlayer::onTalkerStop */

void SvxPlayer::onAllSamplesFlushed(void)
{
  ReflectorClient::onAllSamplesFlushed();
  // Notify the encoder that the far end ACKed the flush.
  if (ReflectorClient::isLoggedIn() && m_enc)
  {
    m_enc->allEncodedSamplesFlushed();
  }
} /* SvxPlayer::onAllSamplesFlushed */


void SvxPlayer::onCodecNegotiated(const std::string& negotiated_codec)
{
    // Initialize (or re-initialize) the encoder/decoder pipeline for the
    // codec agreed with the reflector during login.
  if (!setAudioCodec(negotiated_codec))
  {
    cerr << m_name << ": *** WARNING: Could not set up codec \""
         << negotiated_codec
         << "\". Falling back to DUMMY." << endl;
    setAudioCodec("DUMMY");
  }
} /* SvxPlayer::onCodecNegotiated */

void SvxPlayer::startNextPlayback(void)
{
  if (m_play_queue.empty() || !ReflectorClient::isLoggedIn())
  {
    return;
  }

  PlayRequest req = m_play_queue.front();
  m_play_queue.pop();

  uint32_t tg = (req.tg > 0) ? req.tg : m_default_tg;
  if (tg == 0)
  {
    cerr << m_name << ": *** WARNING: No TG configured for playback. "
         << "Set DEFAULT_TG in config or specify TG in play command." << endl;
  }
  ReflectorClient::selectTg(tg);

  m_playing = true;

  if (!req.cw_msg.empty())
  {
    cout << m_name << ": Playing CW '" << req.cw_msg
         << "' on TG #" << tg << endl;

    int wpm = (req.cw_wpm > 0) ? req.cw_wpm : 20;
    int pitch = (req.cw_pitch > 0) ? req.cw_pitch : 800;

    int short_len = 60000 / (50 * wpm);
    int long_len = short_len * 3;
    int char_spacing = short_len;
    int letter_spacing = short_len * 3;
    int word_spacing = short_len * 7;
    int amp = 500; // 0.5 amplitude

    m_msg_handler->begin();

    if (m_cw_preamble_ms > 0)
    {
      m_msg_handler->playSilence(static_cast<int>(m_cw_preamble_ms));
    }

    bool first_letter = true;
    bool last_was_space = false;

    for (char c : req.cw_msg)
    {
      if (c == ' ')
      {
        last_was_space = true;
        continue;
      }

      const char* morse = getMorse(c);
      if (!morse || !*morse) continue;

      if (!first_letter)
      {
        if (last_was_space) {
          m_msg_handler->playSilence(word_spacing);
        } else {
          m_msg_handler->playSilence(letter_spacing);
        }
      }
      first_letter = false;
      last_was_space = false;

      bool first_char = true;
      for (const char* p = morse; *p; ++p)
      {
        if (!first_char)
        {
          m_msg_handler->playSilence(char_spacing);
        }
        first_char = false;

        if (*p == '.')
        {
          m_msg_handler->playTone(pitch, amp, short_len);
        }
        else if (*p == '-')
        {
          m_msg_handler->playTone(pitch, amp, long_len);
        }
      }
    }

    if (m_cw_postamble_ms > 0)
    {
      m_msg_handler->playSilence(static_cast<int>(m_cw_postamble_ms));
    }

    m_msg_handler->end();
  }
  else
  {
    cout << m_name << ": Playing '" << req.file
         << "' on TG #" << tg << endl;
    m_msg_handler->playFile(req.file);
  }
} /* SvxPlayer::startNextPlayback */


void SvxPlayer::allMsgsWritten(void)
{
  cout << m_name << ": All messages written (playback complete)" << endl;
  m_playing = false;

  if (!m_play_queue.empty())
  {
    uint32_t gap = m_play_queue.front().gap_before_ms;
    if (gap > 0)
    {
      cout << m_name << ": Waiting " << gap << " ms before next file" << endl;
      m_playing = true;
      m_gap_timer.setTimeout(static_cast<int>(gap));
      m_gap_timer.setEnable(true);
    }
    else
    {
      startNextPlayback();
    }
  }
} /* SvxPlayer::allMsgsWritten */


void SvxPlayer::onGapTimerExpired(Async::Timer*)
{
  m_gap_timer.setEnable(false);
  m_playing = false;
  startNextPlayback();
} /* SvxPlayer::onGapTimerExpired */


void SvxPlayer::onPtyData(const void* buf, size_t len)
{
  const char* chars = reinterpret_cast<const char*>(buf);
  static string line_buf;
  for (size_t i = 0; i < len; ++i)
  {
    if (chars[i] == '\n')
    {
      processCommand(line_buf);
      line_buf.clear();
    }
    else if (chars[i] != '\r')
    {
      line_buf += chars[i];
    }
  }
} /* SvxPlayer::onPtyData */


void SvxPlayer::processCommand(const string& line)
{
  if (line.empty())
  {
    return;
  }

  istringstream iss(line);
  string cmd;
  iss >> cmd;
  transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

  if (cmd == "STOP")
  {
    stop();
    return;
  }

  if (cmd == "PLAY")
  {
    string token1, token2, token3;
    if (!(iss >> token1))
    {
      cerr << m_name << ": PLAY command missing arguments" << endl;
      return;
    }

    bool has2 = (bool)(iss >> token2);
    bool has3 = (bool)(iss >> token3);

    auto splitCSV = [](const string& s) {
      vector<string> out;
      istringstream ss(s);
      string f;
      while (getline(ss, f, ','))
      {
        if (!f.empty()) out.push_back(f);
      }
      return out;
    };

    if (!has2)
    {
      playFiles(splitCSV(token1), 0, 0);
    }
    else if (!has3)
    {
      uint32_t tg = 0;
      try { tg = static_cast<uint32_t>(stoul(token1)); }
      catch (const exception&)
      {
        cerr << m_name << ": Invalid TG in PLAY command: " << token1 << endl;
        return;
      }
      playFiles(splitCSV(token2), tg, 0);
    }
    else
    {
      uint32_t tg = 0;
      try { tg = static_cast<uint32_t>(stoul(token1)); }
      catch (const exception&)
      {
        cerr << m_name << ": Invalid TG in PLAY command: " << token1 << endl;
        return;
      }
      uint32_t gap_s = 0;
      try { gap_s = static_cast<uint32_t>(stoul(token2)); }
      catch (const exception&)
      {
        cerr << m_name << ": Invalid GAP in PLAY command: " << token2 << endl;
        return;
      }
      playFiles(splitCSV(token3), tg, gap_s * 1000);
    }
    return;
  }

  if (cmd == "CW")
  {
    string token1, token2, token3;
    if (!(iss >> token1 >> token2 >> token3))
    {
      cerr << m_name << ": CW command missing arguments "
           << "(expected: CW TG WPM PITCH MESSAGE)" << endl;
      return;
    }

    uint32_t tg = 0;
    int wpm = 0;
    int pitch = 0;
    try { tg = static_cast<uint32_t>(stoul(token1)); }
    catch (const exception&)
    {
      cerr << m_name << ": Invalid TG in CW command: " << token1 << endl;
      return;
    }
    try { wpm = stoi(token2); }
    catch (const exception&)
    {
      cerr << m_name << ": Invalid WPM in CW command: " << token2 << endl;
      return;
    }
    try { pitch = stoi(token3); }
    catch (const exception&)
    {
      cerr << m_name << ": Invalid Pitch in CW command: " << token3 << endl;
      return;
    }

    string msg;
    getline(iss, msg);
    size_t first_non_space = msg.find_first_not_of(" \t\r\n");
    if (first_non_space != string::npos)
    {
      msg.erase(0, first_non_space);
    }
    else
    {
      msg.clear();
    }

    size_t last_non_space = msg.find_last_not_of(" \t\r\n");
    if (last_non_space != string::npos)
    {
      msg.erase(last_non_space + 1);
    }

    if (msg.empty())
    {
      cerr << m_name << ": CW command missing message" << endl;
      return;
    }

    playCw(wpm, pitch, msg, tg);
    return;
  }

  cerr << m_name << ": Unknown PTY command: " << cmd << endl;
} /* SvxPlayer::processCommand */


/*
 * This file has not been truncated
 */
