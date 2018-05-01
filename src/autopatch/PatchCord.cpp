// Copyright 2015-2021 by Artem Prilutskiy

#include "PatchCord.h"

#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <stdio.h>

#define SERVICE_NAME    "me.burnaway.BrandMeister"
#define OBJECT_PATH     "/me/burnaway/BrandMeister"
#define INTERFACE_NAME  "me.burnaway.BrandMeister"

// From AutoPatch.cpp
#define AUTOPATCH_LINK_NAME  "AutoPatch"

// From PatchCord.h
#define VALUE_CORD_OUTGOING_SOURCE_ID  1
#define VALUE_CORD_INCOMING_SOURCE_ID  4

#define BANNER_RENEWAL_INTERVAL  60
#define BANNER_BUFFER_LENGTH     40

PatchCord::PatchCord(uint32_t network, uint32_t link)
{
  renewal = 0;
  number = link;
  banner = (char*)calloc(BANNER_BUFFER_LENGTH, 1);
  asprintf(&name, SERVICE_NAME ".N%d", network);
  connection = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);
}

PatchCord::~PatchCord()
{
  dbus_connection_unref(connection);
  free(banner);
  free(name);
}

void PatchCord::setTalkerID(uint32_t value)
{
  getContextBanner();
  setSpecificValue(VALUE_CORD_OUTGOING_SOURCE_ID, value);
}

uint32_t PatchCord::getTalkerID()
{
  getContextBanner();
  return getSpecificValue(VALUE_CORD_INCOMING_SOURCE_ID);
}

void PatchCord::setTalkerAlias(const char* value)
{
  size_t length = strlen(value) + 12;
  char* buffer = (char*)alloca(length);
  sprintf(buffer, "set alias %s", value);

  getContextBanner();
  invokeCommand(buffer);
}

void PatchCord::getContextBanner()
{
  time_t now = time(NULL);

  if (renewal < now)
  {
    DBusMessage* request = dbus_message_new_method_call(
      name, OBJECT_PATH, INTERFACE_NAME, "getContextList");

    const char* name = AUTOPATCH_LINK_NAME;

    dbus_message_append_args(request,
      DBUS_TYPE_STRING, &name,
      DBUS_TYPE_UINT32, &number,
      DBUS_TYPE_INVALID);

    DBusMessage* response = makeCall(request);

    if (response != NULL)
    {
      char** array;
      int count;

      if ((dbus_message_get_args(response, NULL,
            DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &array, &count,
            DBUS_TYPE_INVALID)) &&
          (count > 0))
      {
        strncpy(banner, array[0], BANNER_BUFFER_LENGTH);
        renewal = now + BANNER_RENEWAL_INTERVAL;
        dbus_free_string_array(array);
      }

      dbus_message_unref(response);
    }
  }
}

void PatchCord::invokeCommand(const char* command)
{
  if (*banner != '\0')
  {
    DBusMessage* request = dbus_message_new_method_call(
      name, OBJECT_PATH, INTERFACE_NAME, "invokeCommand");

    dbus_message_append_args(request,
      DBUS_TYPE_STRING, &banner,
      DBUS_TYPE_STRING, &command,
      DBUS_TYPE_INVALID);
    
    makeCall(request, true);
  }
}

void PatchCord::setSpecificValue(uint32_t key, uint32_t value)
{
  if (*banner != '\0')
  {
    DBusMessage* request = dbus_message_new_method_call(
      name, OBJECT_PATH, INTERFACE_NAME, "setSpecificValue");

    dbus_message_append_args(request,
      DBUS_TYPE_STRING, &banner,
      DBUS_TYPE_UINT32, &key,
      DBUS_TYPE_UINT32, &value,
      DBUS_TYPE_INVALID);

    makeCall(request, true);
  }
}

uint32_t PatchCord::getSpecificValue(uint32_t key)
{
  uint32_t value = 0;

  if (*banner != '\0')
  {
    DBusMessage* request = dbus_message_new_method_call(
      name, OBJECT_PATH, INTERFACE_NAME, "getContextData");

    dbus_message_append_args(request,
      DBUS_TYPE_STRING, &banner,
      DBUS_TYPE_INVALID);

    DBusMessage* response = makeCall(request);

    if (response != NULL)
    {
      char* name;
      char* dummy;
      char* address;
      dbus_uint32_t type;
      dbus_uint32_t mode;
      dbus_uint32_t state;
      dbus_uint32_t number;
      dbus_uint32_t* values;
      int count;

      if (dbus_message_get_args(response, NULL,
            DBUS_TYPE_STRING, &dummy,
            DBUS_TYPE_STRING, &name,
            DBUS_TYPE_UINT32, &type,
            DBUS_TYPE_UINT32, &number,
            DBUS_TYPE_STRING, &address,
            DBUS_TYPE_UINT32, &mode,
            DBUS_TYPE_UINT32, &state,
            DBUS_TYPE_ARRAY, DBUS_TYPE_UINT32, &values, &count,
            DBUS_TYPE_INVALID))
        value = values[key];

      dbus_message_unref(response);
    }
  }

  return value;
}

uint32_t PatchCord::getPrivateIDForCall(const char* call)
{
  uint32_t value = 0;

  DBusMessage* request = dbus_message_new_method_call(
    name, OBJECT_PATH, INTERFACE_NAME, "getStationData");

  dbus_message_append_args(request,
    DBUS_TYPE_STRING, &call,
    DBUS_TYPE_INVALID);

  DBusMessage* response = makeCall(request);

  if (response != NULL)
  {
    dbus_bool_t active;
    dbus_uint32_t number;
    dbus_uint32_t algorithm;
    dbus_uint32_t key;
    dbus_uint32_t interval;
    dbus_uint32_t capabilities;
    dbus_uint32_t station;
    const char* language;
    const char* call;
    const char* text;
    const char* symbol;

    if (dbus_message_get_args(response, NULL,
          DBUS_TYPE_UINT32, &number,
          DBUS_TYPE_UINT32, &algorithm,
          DBUS_TYPE_UINT32, &key,
          DBUS_TYPE_UINT32, &interval,
          DBUS_TYPE_UINT32, &capabilities,
          DBUS_TYPE_STRING, &language,
          DBUS_TYPE_UINT32, &station,
          DBUS_TYPE_STRING, &call,
          DBUS_TYPE_STRING, &text,
          DBUS_TYPE_STRING, &symbol,
          DBUS_TYPE_BOOLEAN, &active,
          DBUS_TYPE_INVALID))
      value = number;

    dbus_message_unref(response);
  }

  return value;
}

bool PatchCord::getCredentialsForID(uint32_t number, char* call, char* text)
{
  bool result = false;

  DBusMessage* request = dbus_message_new_method_call(
    name, OBJECT_PATH, INTERFACE_NAME, "getStationData");

  dbus_message_append_args(request,
    DBUS_TYPE_UINT32, &number,
    DBUS_TYPE_INVALID);

  DBusMessage* response = makeCall(request);

  if (response != NULL)
  {
    dbus_bool_t active;
    dbus_uint32_t number;
    dbus_uint32_t algorithm;
    dbus_uint32_t key;
    dbus_uint32_t interval;
    dbus_uint32_t capabilities;
    dbus_uint32_t station;
    const char* language;
    const char* value1;
    const char* value2;
    const char* symbol;

    if (dbus_message_get_args(response, NULL,
          DBUS_TYPE_UINT32, &number,
          DBUS_TYPE_UINT32, &algorithm,
          DBUS_TYPE_UINT32, &key,
          DBUS_TYPE_UINT32, &interval,
          DBUS_TYPE_UINT32, &capabilities,
          DBUS_TYPE_STRING, &language,
          DBUS_TYPE_UINT32, &station,
          DBUS_TYPE_STRING, &value1,
          DBUS_TYPE_STRING, &value2,
          DBUS_TYPE_STRING, &symbol,
          DBUS_TYPE_BOOLEAN, &active,
          DBUS_TYPE_INVALID))
    {
      strcpy(call, value1);
      strcpy(text, value2);
      result = true;
    }

    dbus_message_unref(response);
  }

  return result;
}

DBusMessage* PatchCord::makeCall(DBusMessage* request, bool omission)
{
  DBusPendingCall* pending;
  DBusMessage* response = NULL;
  if (dbus_connection_send_with_reply(connection, request, &pending, -1))
  {
    dbus_connection_flush(connection);
    dbus_pending_call_block(pending);
    response = dbus_pending_call_steal_reply(pending);
    dbus_pending_call_unref(pending);
  }
  dbus_message_unref(request);
  if (omission && response)
  {
    dbus_message_unref(response);
    return NULL;
  }
  return response;
}
