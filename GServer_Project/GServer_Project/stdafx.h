#pragma once
#include <iostream>
#include "2021_°¡À»_protocol.h"
#include "enum.h"
#include <string>
#include <vector>
#include <WS2tcpip.h>

#include <MSWSock.h>
#include <thread>
#include <array>
#include <mutex>
#include <unordered_set>
#include <concurrent_priority_queue.h>

extern "C" {
#include "include/lua.h"
#include "include/lauxlib.h"
#include "include/lualib.h"
}
#pragma comment (lib, "lua54.lib")

#pragma comment (lib, "WS2_32.LIB")
#pragma comment (lib, "MSWSock.LIB")

#define NAME_LEN 50 

const int BUFSIZE = 256;
const int RANGE = 15;

using namespace std;

