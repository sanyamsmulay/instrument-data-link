#pragma once

#ifdef _WIN32
#include <Windows.h>
#else
#include "../headers/SimConnectTypes.h"
#endif

#include <future>
#include <map>

#include "Protocol.h"

namespace jetbridge {

class Client {
 private:
  HANDLE simconnect = 0;

 public:
  Client(HANDLE simconnect);
  void request(const char data[]);
};

}  // namespace jetbridge
