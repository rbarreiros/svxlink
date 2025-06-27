// Copyright 2015-2019 by Artem Prilutskiy

#include "BrandMeisterBridge.h"
#include <string.h>
#include <stdlib.h>
#include <syslog.h>
#include <stdio.h>

#define ECHOLINK_DEFAULT_USER_CALL    "N0CALL Unknown call"
#define ECHOLINK_DEFAULT_USER_NUMBER  1

#define CALL_BUFFER_SIZE    16
#define TEXT_BUFFER_SIZE    48
#define TALKER_BUFFER_SIZE  80

#define DELETE(object)  \
  if (object)           \
  delete object;

BrandMeisterBridge::BrandMeisterBridge()
{
  proxy   = NULL;
  handle  = NULL;
  node    = NULL;
  length  = 0;
  talker  = (char*)malloc(TALKER_BUFFER_SIZE);
  unknown = ECHOLINK_DEFAULT_USER_NUMBER;
}

BrandMeisterBridge::~BrandMeisterBridge()
{
  iconv_close(handle);
  DELETE(proxy);
  free(talker);
  free(node);
}

// Interface methods for ModuleEchoLink

void BrandMeisterBridge::setEncodingConfiguration(const char* configuration)
{
  handle = iconv_open("UTF-8", configuration);
}

void BrandMeisterBridge::setDefaultConfiguration(const char* configuration)
{
  unknown = strtol(configuration, NULL, 10);
}

void BrandMeisterBridge::setCallConfiguration(const char* configuration)
{
  node   = strdup(configuration);
  length = strlen(configuration);
}

void BrandMeisterBridge::setProxyConfiguration(const char* configuration)
{
  char* pointer = const_cast<char*>(configuration);
  // <Network ID>:<PatchCord ID>
  uint32_t network = strtol(pointer + 0, &pointer, 10);
  uint32_t link    = strtol(pointer + 1, &pointer, 10);
  proxy = new PatchCord(network, link);
}

const char* BrandMeisterBridge::getTalker()
{
  if (proxy == NULL)
  {
    syslog(LOG_ERR, "BrandMeister bridge is not configured");
    return ECHOLINK_DEFAULT_USER_CALL;
  }

  uint32_t number = proxy->getTalkerID();

  char call[CALL_BUFFER_SIZE];
  char text[TEXT_BUFFER_SIZE];

  if ((number != 0) &&
      (proxy->getCredentialsForID(number, call, text)))
  {
    snprintf(talker, TALKER_BUFFER_SIZE, "%s %s", call, text);
    return talker;
  }

  snprintf(talker, TALKER_BUFFER_SIZE, "DMR ID: %d", number);
  return talker;
}

void BrandMeisterBridge::setTalker(const char* call, const char* name)
{
  if (proxy == NULL)
  {
    syslog(LOG_ERR, "BrandMeister bridge is not configured");
    return;
  }

  if (*call == '*')
  {
    // Do not handle conference call-sign
    return;
  }

  size_t length = strlen(call) + strlen(name);
  char* buffer = (char*)alloca(length + sizeof(uint32_t));

  sprintf(buffer, "%s %s", call, name);

  setTalkerData(call, buffer);
}

void BrandMeisterBridge::handleChatMessage(const char* text)
{
  // CONF Russian Reflector, Open 24/7, Contacts: rv3dhc.link@qip.ru * Call CQ / Use pauses 2sec * [28/500]
  // R3ABM-L *DSTAR.SU DMR Bridge*
  // UB3AMO Moscow T I N A O
  // ->UA0LQE-L USSURIISK

  if (proxy == NULL)
  {
    syslog(LOG_ERR, "BrandMeister bridge is not configured");
    return;
  }

  if (strncmp(text, "CONF ", 5) == 0)
  {
    const char* delimiter = strstr(text, "\n->");
    const char* call      = delimiter + 3;
    if ((delimiter != NULL) &&
        ((node == NULL) || (strncmp(call, node, length) != 0)))
    {
      // Don't pass local node callsign
      setTalkerData(call, call);
    }
    else
    {
      syslog(LOG_INFO, "Set talker ID to %d (call-sign is not present in chat message)", unknown);
      proxy->setTalkerID(unknown);
      proxy->setTalkerAlias("");
    }
  }
}

void BrandMeisterBridge::setTalkerData(const char* call, const char* name)
{
  const char* delimiter1 = strpbrk(call, " -\n");
  const char* delimiter2 = strchr(name, '\n');

  if (delimiter1 != NULL)
  {
    // Remove characters after call-sign
    size_t length = delimiter1 - call;
    char* buffer = (char*)alloca(length + sizeof(uint32_t));
    strncpy(buffer, call, length);
    buffer[length] = '\0';
    call = buffer;
  }

  if (delimiter2 != NULL)
  {
    // Remove characters after talker name
    size_t length = delimiter2 - name;
    char* buffer = (char*)alloca(length + sizeof(uint32_t));
    strncpy(buffer, name, length);
    buffer[length] = '\0';
    name = buffer;
  }

  if (handle != NULL)
  {
    // Convert name to UTF8
    size_t length1 = strlen(name);
    size_t length2 = length1 * 3;
    char* buffer = (char*)alloca(length2);
    char* pointer1 = const_cast<char*>(name);
    char* pointer2 = buffer;
    iconv(handle, &pointer1, &length1, &pointer2, &length2);
    *pointer2 = '\0';
    name = buffer;
  }

  uint32_t number = proxy->getPrivateIDForCall(call);

  if (number == 0)
  {
    // Use default value instead if ID not found
    number = unknown;
  }

  proxy->setTalkerID(number);
  proxy->setTalkerAlias(name);

  syslog(LOG_INFO, "Set talker ID to %d for call-sign %s (%s)", number, call, name);
}
