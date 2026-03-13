#pragma once
#include <iostream>
#include "protocol.h"
#include "enum.h"
#include <string>
#include <vector>
#include <WS2tcpip.h>

#include <MSWSock.h>
#include <thread>
#include <array>
#include <mutex>
#include <functional>
#include <unordered_set>
#include <unordered_map>
#include <deque>
#include <memory>
#include <chrono>
#include <concurrent_priority_queue.h>

#include "game_protocol_generated.h"
#include "Packet.h"
#include "ObjectQueue.h"
#include "InnerPacket.h"

extern "C" {
#include "include/lua.h"
#include "include/lauxlib.h"
#include "include/lualib.h"
}
#pragma comment (lib, "lua54.lib")

#pragma comment (lib, "WS2_32.LIB")
#pragma comment (lib, "MSWSock.LIB")

#define SafeDelete(x)   if (x != nullptr) { delete x; x = nullptr; }
#define NAME_LEN 50 

const int BUFSIZE = 256;
const int RANGE = 15;

using namespace std;

