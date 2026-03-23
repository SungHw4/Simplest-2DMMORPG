#pragma once

const short SERVER_PORT = 4000;

const int  WORLD_HEIGHT = 2000;
const int  WORLD_WIDTH = 2000;
const int  MAX_NAME_SIZE = 20;
const int  MAX_CHAT_SIZE = 100;
const int  MAX_USER = 10000;
const int  MAX_NPC = 200000;
//const int  MAX_NPC = 10000;
constexpr int NPC_ID_START = MAX_USER;
constexpr int NPC_ID_END = MAX_USER + MAX_NPC - 1;

const char CS_PACKET_LOGIN = 1;
const char CS_PACKET_MOVE = 2;

const char SC_PACKET_LOGIN_OK = 1;
const char SC_PACKET_MOVE = 2;
const char SC_PACKET_PUT_OBJECT = 3;
const char SC_PACKET_REMOVE_OBJECT = 4;
const char SC_PACKET_CHAT = 5;
const char SC_PACKET_LOGIN_NO = 6;
#pragma pack (push, 1)
struct cs_packet_login {
	unsigned char size;
	char	type;
	char	name[MAX_NAME_SIZE];
};

struct cs_packet_move {
	unsigned char size;
	char	type;
	char	direction;			// 0 : up,  1: down, 2:left, 3:right
	int		move_time;
};

struct sc_packet_login_ok {
	unsigned char size;
	char type;
	int		id;
	short	x, y;
};

struct sc_packet_move {
	unsigned char size;
	char type;
	int		id;
	short  x, y;
	int		move_time;
};

struct sc_packet_put_object {
	unsigned char size;
	char type;
	int id;
	short x, y;
	char object_type;
	char	name[MAX_NAME_SIZE];
};

struct sc_packet_remove_object {
	unsigned char size;
	char type;
	int id;
};

struct sc_packet_chat {
	unsigned char size;
	char type;
	int id;
	char message[MAX_CHAT_SIZE];
};
#pragma pack(pop)
