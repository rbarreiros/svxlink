/**
@file	 RemoteUserAuth.cpp
@brief   Async wrapper for Paho MQTT C++ client
@author  Rui Barreiros / CR7BPM
@date	 2026-01-07

\verbatim
SvxReflector - An audio reflector for connecting SvxLink Servers
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

#include <iostream>
#include <sstream>
#include <cstring>
#include <json/json.h>
#include <curl/curl.h>

#include "RemoteUserAuth.h"

using namespace std;
using namespace Async;

void RemoteUserAuth::setParams(const string& auth_url, const string& auth_token,
                               bool force_valid_ssl)
{
  m_auth_url = auth_url;
  m_auth_token = auth_token;
  m_force_valid_ssl = force_valid_ssl;
}


RemoteUserAuth::RemoteUserAuth(void)
  : m_force_valid_ssl(true), m_update_timer(100, Timer::TYPE_PERIODIC, false)
{
  curl_global_init(CURL_GLOBAL_DEFAULT);
  m_multi_handle = curl_multi_init();
  m_update_timer.expired.connect(sigc::mem_fun(*this, &RemoteUserAuth::onTimeout));
}

RemoteUserAuth::~RemoteUserAuth(void)
{
  m_update_timer.setEnable(false);
  
  for (auto const& [curl, req] : m_request_map)
  {
    curl_multi_remove_handle(m_multi_handle, curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(req->headers);
    delete req;
  }
  m_request_map.clear();

  for (auto const& [fd, ws] : m_watch_map)
  {
    delete ws;
  }
  m_watch_map.clear();

  curl_multi_cleanup(m_multi_handle);
  curl_global_cleanup();
}

void RemoteUserAuth::checkUser(const string& username, const string& digest,
                               const string& challenge,
                               sigc::slot<void(bool, string)> callback)
{
  CURL* curl = curl_easy_init();
  if (!curl)
  {
    callback(false, "Failed to initialize curl");
    return;
  }

  Request* req = new Request();
  req->curl = curl;
  req->callback = callback;
  req->headers = NULL;

  Json::Value root;
  root["username"] = username;
  root["digest"] = digest;
  root["challenge"] = challenge;
  
  Json::StreamWriterBuilder wbuilder;
  req->post_data = Json::writeString(wbuilder, root);

  string auth_header = "Authorization: Bearer " + m_auth_token;
  req->headers = curl_slist_append(req->headers, "Content-Type: application/json");
  req->headers = curl_slist_append(req->headers, auth_header.c_str());

  curl_easy_setopt(curl, CURLOPT_URL, m_auth_url.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->post_data.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, req->headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &RemoteUserAuth::writeCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, req);
  curl_easy_setopt(curl, CURLOPT_PRIVATE, req);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

  if (!m_force_valid_ssl)
  {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  }

  m_request_map[curl] = req;
  curl_multi_add_handle(m_multi_handle, curl);

  updateWatchMap();
  m_update_timer.setEnable(true);
}

void RemoteUserAuth::onTimeout(Timer *timer)
{
  int running_handles;
  curl_multi_perform(m_multi_handle, &running_handles);
  checkMultiInfo();
  updateWatchMap();
  if (running_handles == 0)
  {
    m_update_timer.setEnable(false);
  }
}

void RemoteUserAuth::onActivity(FdWatch *watch)
{
  int running_handles;
  curl_multi_perform(m_multi_handle, &running_handles);
  checkMultiInfo();
  updateWatchMap();
  if (running_handles == 0)
  {
    m_update_timer.setEnable(false);
  }
}

void RemoteUserAuth::updateWatchMap(void)
{
  fd_set fdread;
  fd_set fdwrite;
  fd_set fdexcep;
  int maxfd = -1;

  FD_ZERO(&fdread);
  FD_ZERO(&fdwrite);
  FD_ZERO(&fdexcep);

  curl_multi_fdset(m_multi_handle, &fdread, &fdwrite, &fdexcep, &maxfd);

  // First, mark all existing watches as old
  for (auto const& [fd, ws] : m_watch_map)
  {
    ws->rd_enabled = false;
    ws->wr_enabled = false;
  }

  for (int fd = 0; fd <= maxfd; fd++)
  {
    bool r_set = FD_ISSET(fd, &fdread);
    bool w_set = FD_ISSET(fd, &fdwrite);

    if (r_set || w_set)
    {
      WatchSet* ws = m_watch_map[fd];
      if (!ws)
      {
        ws = new WatchSet();
        m_watch_map[fd] = ws;
      }

      if (r_set)
      {
        if (!ws->rd.isEnabled())
        {
          ws->rd.setFd(fd, FdWatch::FD_WATCH_RD);
          ws->rd.activity.connect(sigc::mem_fun(*this, &RemoteUserAuth::onActivity));
          ws->rd.setEnabled(true);
        }
        ws->rd_enabled = true;
      }
      
      if (w_set)
      {
        if (!ws->wr.isEnabled())
        {
          ws->wr.setFd(fd, FdWatch::FD_WATCH_WR);
          ws->wr.activity.connect(sigc::mem_fun(*this, &RemoteUserAuth::onActivity));
          ws->wr.setEnabled(true);
        }
        ws->wr_enabled = true;
      }
    }
  }

  // Remove watches that are no longer needed
  for (auto it = m_watch_map.begin(); it != m_watch_map.end(); )
  {
    WatchSet* ws = it->second;
    if (!ws->rd_enabled && ws->rd.isEnabled())
    {
      ws->rd.setEnabled(false);
    }
    if (!ws->wr_enabled && ws->wr.isEnabled())
    {
      ws->wr.setEnabled(false);
    }
    
    if (!ws->rd_enabled && !ws->wr_enabled)
    {
      delete ws;
      it = m_watch_map.erase(it);
    }
    else
    {
      ++it;
    }
  }
}

void RemoteUserAuth::checkMultiInfo(void)
{
  int msgs_left;
  CURLMsg* msg;
  while ((msg = curl_multi_info_read(m_multi_handle, &msgs_left)))
  {
    if (msg->msg == CURLMSG_DONE)
    {
      CURL* curl = msg->easy_handle;
      Request* req = m_request_map[curl];
      if (req)
      {
        bool success = false;
        string message = "Unknown error";
        
        if (msg->data.result == CURLE_OK)
        {
          Json::Value root;
          Json::CharReaderBuilder rbuilder;
          string errs;
          istringstream iss(req->response_data);
          
          if (Json::parseFromStream(rbuilder, iss, &root, &errs))
          {
            success = root["success"].asBool();
            message = root["message"].asString();
          }
          else
          {
            message = "Failed to parse JSON response: " + errs;
          }
        }
        else
        {
          message = string("CURL error: ") + curl_easy_strerror(msg->data.result);
        }

        req->callback(success, message);

        curl_multi_remove_handle(m_multi_handle, curl);
        curl_easy_cleanup(curl);
        curl_slist_free_all(req->headers);
        m_request_map.erase(curl);
        delete req;
      }
    }
  }
}

size_t RemoteUserAuth::writeCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  Request* req = static_cast<Request*>(userp);
  req->response_data.append(static_cast<char*>(contents), realsize);
  return realsize;
}
