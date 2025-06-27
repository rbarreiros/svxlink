// Copyright 2015 by Artem Prilutskiy

#ifndef PATCHCORDPROXY_H
#define PATCHCORDPROXY_H

#include <time.h>
#include <stdint.h>
#include <dbus/dbus.h>

class PatchCord
{
  public:

    PatchCord(uint32_t network, uint32_t link);
    ~PatchCord();

    // Primary methods to manage PatchCord instance

    void setTalkerID(uint32_t value);
    void setTalkerAlias(const char* value);
    uint32_t getTalkerID();

    // Supplimentary methods to access cached data from database

    uint32_t getPrivateIDForCall(const char* call);
    bool getCredentialsForID(uint32_t number, char* call, char* text);

  private:

    DBusConnection* connection;
    char* name;

    uint32_t number;
    time_t renewal;
    char* banner;

    void getContextBanner();
    void invokeCommand(const char* command);
    void setSpecificValue(uint32_t key, uint32_t value);
    uint32_t getSpecificValue(uint32_t key);

    DBusMessage* makeCall(DBusMessage* request, bool omission = false);

};

#endif