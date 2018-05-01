// Copyright 2015 by Artem Prilutskiy

#ifndef BRANDMEISTERBRIDGE_H
#define BRANDMEISTERBRIDGE_H

#include <iconv.h>

#include "PatchCord.h"

class BrandMeisterBridge
{
  public:

    BrandMeisterBridge();
    ~BrandMeisterBridge();

    void setEncodingConfiguration(const char* configuration);
    void setDefaultConfiguration(const char* configuration);
    void setProxyConfiguration(const char* configuration);
    void setCallConfiguration(const char* configuration);

    const char* getTalker();
    void setTalker(const char* call, const char* name);
    void handleChatMessage(const char* text);

  private:

    PatchCord* proxy;
    iconv_t handle;
    char* talker;
    char* node;
    int length;
    int unknown;

    void setTalkerData(const char* call, const char* name);

};

#endif