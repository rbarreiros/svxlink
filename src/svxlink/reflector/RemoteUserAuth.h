/**
@file    RemoteUserAuth.h
@brief   Handles remote user authentication via a web API
@author  Rui Barreiros / CR7BPM
@date    2026-01-02
*/

#ifndef REMOTE_USER_AUTH_INCLUDED
#define REMOTE_USER_AUTH_INCLUDED


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <string>
#include <map>
#include <queue>
#include <curl/curl.h>
#include <sigc++/sigc++.h>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncFdWatch.h>
#include <AsyncTimer.h>


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
 * Class definitions
 *
 ****************************************************************************/

/**
@brief   Handles remote user authentication via a web API
@author  Antigravity
@date    2026-01-02

This class performs asynchronous HTTP POST requests to a remote authentication
server. It uses libcurl's multi-interface to avoid blocking the main event
loop.
*/
class RemoteUserAuth : public sigc::trackable
{
  public:
    /**
     * @brief   Get the class instance
     */
    static RemoteUserAuth* instance(void);

    /**
     * @brief   Set parameters for the remote authentication
     * @param   auth_url The URL of the authentication endpoint
     * @param   auth_token The Bearer token for authentication
     * @param   force_valid_ssl Whether to enforce valid SSL certificates
     */
    void setParams(const std::string& auth_url, const std::string& auth_token,
                   bool force_valid_ssl);

    /**
     * @brief   Check if a user is authenticated on the remote server
     * @param   username The username to check
     * @param   digest Hex encoded HMAC digest
     * @param   challenge Hex encoded challenge
     * @param   callback The callback to call when the check is complete
     */
    void checkUser(const std::string& username, const std::string& digest,
                   const std::string& challenge,
                   sigc::slot<void(bool, std::string)> callback);

  private:
    /**
     * @brief   Constructor
     */
    RemoteUserAuth(void);

    /**
     * @brief   Destructor
     */
    ~RemoteUserAuth(void);

    struct WatchSet
    {
      WatchSet() : rd_enabled(false), wr_enabled(false) {}
      Async::FdWatch rd;
      Async::FdWatch wr;
      bool rd_enabled;
      bool wr_enabled;
    };

    struct Request
    {
      CURL* curl;
      curl_slist* headers;
      std::string post_data;
      std::string response_data;
      sigc::slot<void(bool, std::string)> callback;
    };

    static RemoteUserAuth* m_instance;

    std::string m_auth_url;
    std::string m_auth_token;
    bool        m_force_valid_ssl;
    CURLM*      m_multi_handle;
    Async::Timer m_update_timer;
    std::map<int, WatchSet*> m_watch_map;
    std::map<CURL*, Request*> m_request_map;

    void onTimeout(Async::Timer *timer);
    void onActivity(Async::FdWatch *watch);
    void updateWatchMap(void);
    void checkMultiInfo(void);
    static size_t writeCallback(void *contents, size_t size, size_t nmemb, void *userp);

};  /* class RemoteUserAuth */


#endif /* REMOTE_USER_AUTH_INCLUDED */
