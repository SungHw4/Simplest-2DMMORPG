#define SFML_STATIC 1
//#include "include/SFML/Graphics.hpp"
#include <SFML/Graphics.hpp>
//#include "include/SFML/Network.hpp"
#include <SFML/Network.hpp>
#include <iostream>
#include <chrono>
#include <array>
using namespace std;

#ifdef _DEBUG
#pragma comment (lib, "lib/sfml-graphics-s-d.lib")
#pragma comment (lib, "lib/sfml-window-s-d.lib")
#pragma comment (lib, "lib/sfml-system-s-d.lib")
#pragma comment (lib, "lib/sfml-network-s-d.lib")
#else
#pragma comment (lib, "lib/sfml-graphics-s.lib")
#pragma comment (lib, "lib/sfml-window-s.lib")
#pragma comment (lib, "lib/sfml-system-s.lib")
#pragma comment (lib, "lib/sfml-network-s.lib")
#endif
#pragma comment (lib, "opengl32.lib")
#pragma comment (lib, "winmm.lib")
#pragma comment (lib, "ws2_32.lib")

#include "..\..\GServer_Project\GServer_Project\2021_가을_protocol.h"

sf::TcpSocket socket;
int obstaclenum = 0;
bool obs[WORLD_HEIGHT][WORLD_WIDTH];
constexpr auto BUF_SIZE = 256;
constexpr auto SCREEN_WIDTH = 20;
constexpr auto SCREEN_HEIGHT = 20;

constexpr auto TILE_WIDTH = 65;
constexpr auto WINDOW_WIDTH = TILE_WIDTH * SCREEN_WIDTH + 10;   // size of window
constexpr auto WINDOW_HEIGHT = TILE_WIDTH * SCREEN_WIDTH + 10;
//constexpr auto BUF_SIZE = MAX_BUFFER;

int g_myid;
int g_x_origin;
int g_y_origin;
bool g_start = 0;
sf::RenderWindow* g_window;
sf::Font g_font;
string name{ "PL" };
class OBJECT {
private:
	bool m_showing;
	sf::Sprite m_sprite;
	sf::Text m_name;
	sf::RectangleShape HPshape;
	sf::Text m_chat;
	chrono::system_clock::time_point m_mess_end_time;
public:
	int m_x, m_y;
	int m_type;
	short level;
	short hp, maxhp = 100;
	int exp;

	OBJECT(sf::Texture& t, int x, int y, int x2, int y2) {
		m_showing = false;
		m_sprite.setTexture(t);
		m_sprite.setTextureRect(sf::IntRect(x, y, x2, y2));
		set_name("NONAME");
		m_mess_end_time = chrono::system_clock::now();
	}
	OBJECT() {
		m_showing = false;
	}
	void show()
	{
		m_showing = true;
	}
	void hide()
	{
		m_showing = false;
	}

	void a_move(int x, int y) {
		m_sprite.setPosition((float)x, (float)y);
	}

	void a_draw() {
		g_window->draw(m_sprite);
	}

	void move(int x, int y) {
		m_x = x;
		m_y = y;
	}
	void draw() {
		if (false == m_showing) return;
		if (m_type == 3) {
			//m_sprite.setTexture(*pieces);
			m_sprite.setTextureRect(sf::IntRect(64,0,64,64));
		}// = OBJECT{ *pieces, 0, 0, 64, 64 };
		float rx = (m_x - g_x_origin) * 65.0f + 8;
		float ry = (m_y - g_y_origin) * 65.0f + 8;
		m_sprite.setPosition(rx, ry);
		g_window->draw(m_sprite);
		if (m_mess_end_time < chrono::system_clock::now()) {
			m_name.setPosition(rx - 10, ry - 20);
			g_window->draw(m_name);

			sf::RectangleShape HP(sf::Vector2f(hp / maxhp * 100, 20));
			HP.setPosition(rx - 10, ry - 40);
			HP.setFillColor(sf::Color(255, 0, 0, 255));
			g_window->draw(HP);
		}
		else {
			m_chat.setPosition(rx - 10, ry - 20);
			g_window->draw(m_chat);
		}
		
	}
	void set_name(const char str[]) {
		m_name.setFont(g_font);
		m_name.setString(str);
		m_name.setFillColor(sf::Color(255, 255, 0));
		m_name.setStyle(sf::Text::Bold);
	}
	void set_chat(const char str[]) {
		m_chat.setFont(g_font);
		m_chat.setString(str);
		m_chat.setFillColor(sf::Color(255, 255, 255));
		m_chat.setStyle(sf::Text::Bold);
		m_mess_end_time = chrono::system_clock::now() + chrono::seconds(3);
	}
};

OBJECT avatar;
OBJECT players[MAX_USER + MAX_NPC];
OBJECT obstacles[12500];
OBJECT white_tile;
OBJECT black_tile;
OBJECT obstacle_tile;
sf::Texture* board;
sf::Texture* obstacle;
sf::Texture* pieces;

void client_initialize()
{
	board = new sf::Texture;
	pieces = new sf::Texture;
	obstacle = new sf::Texture;
	if (false == g_font.loadFromFile("cour.ttf")) {
		cout << "Font Loading Error!\n";
		while (true);
	}
	board->loadFromFile("tilemap.png");
	obstacle->loadFromFile("obstacle.jpg");
	pieces->loadFromFile("chess2.png");
	//white_tile = OBJECT{ *board, 5, 5, TILE_WIDTH, TILE_WIDTH };
	white_tile = OBJECT{ *board, 0, 0, 65, 65};
	black_tile = OBJECT{ *board, 65, 65, 65+TILE_WIDTH, 65+TILE_WIDTH };
	obstacle_tile = OBJECT{ *obstacle, 0, 0, 65, 65};
	avatar = OBJECT{ *pieces, 128, 0, 65, 65 };
	for (auto& pl : players) {
		pl = OBJECT{ *pieces, 0, 0, 65, 65 };
	}

	srand(2);
	for (int i = 0; i < WORLD_HEIGHT/4; ++i)
	{
		for (int j = 0; j < WORLD_WIDTH/4; ++j)
		{
			obs[i][j] = rand() % 4;
			if (obs[i][j]) obs[i][j] = 0;
			else obs[i][j] = 1;
		}
	}
}

void client_finish()
{
	delete board;
	delete pieces;
	delete obstacle;
}

void ProcessPacket(char* ptr)
{
	static bool first_time = true;
	switch (ptr[1])
	{
	case SC_PACKET_LOGIN_OK:
	{
		g_start = true;
		sc_packet_login_ok* packet = reinterpret_cast<sc_packet_login_ok*>(ptr);
		g_myid = packet->id;
		avatar.m_x = packet->x;
		avatar.m_y = packet->y;
		avatar.exp = packet->exp;
		avatar.hp = packet->hp;
		avatar.maxhp = packet->maxhp;
		avatar.level = packet->level;
		g_x_origin = packet->x - SCREEN_WIDTH / 2;
		g_y_origin = packet->y - SCREEN_WIDTH / 2;
		avatar.move(packet->x, packet->y);
		//memcpy(obs, packet->obs, sizeof(obs));
		avatar.show();
	}
	break;
	/*case SC_PACKET_OBSTACLE:
	{
		sc_packet_obstacle* packet = reinterpret_cast<sc_packet_obstacle*>(ptr);
		int x = packet->x;
		int y = packet->y;
		int id = packet->id;
		obstacles[id].move(x, y);
		obstacles[id].show();
	}*/
	case SC_PACKET_LOGIN_FAIL:
	{
		cout << "해당 ID가 존재하지 않습니다..." << endl;
		socket.disconnect();
		client_finish();
		exit(0);
	}
	break;
	case SC_PACKET_PUT_OBJECT:
	{
		sc_packet_put_object* my_packet = reinterpret_cast<sc_packet_put_object*>(ptr);
		int id = my_packet->id;
		cout << my_packet->hp << endl;
		if (id < MAX_USER) { // PLAYER
			players[id].set_name(my_packet->name);
			players[id].move(my_packet->x, my_packet->y);
			players[id].hp = my_packet->hp;
			players[id].show();
		}
		else {  // NPC
			players[id].set_name(my_packet->name);
			players[id].move(my_packet->x, my_packet->y);
			players[id].m_type = my_packet->object_type;
			players[id].hp = my_packet->hp;
			players[id].show();
		}
		break;
	}
	case SC_PACKET_MOVE:
	{
		cout << "b" << endl;
		sc_packet_move* my_packet = reinterpret_cast<sc_packet_move*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.move(my_packet->x, my_packet->y);
			g_x_origin = my_packet->x - SCREEN_WIDTH / 2;
			g_y_origin = my_packet->y - SCREEN_WIDTH / 2;
		}
		else if (other_id < MAX_USER) {
			players[other_id].move(my_packet->x, my_packet->y);
			players[other_id].hp = my_packet->hp;
		}
		else {
			players[other_id].move(my_packet->x, my_packet->y);
			players[other_id].hp = my_packet->hp;
		}
		break;
	}

	case SC_PACKET_REMOVE_OBJECT:
	{
		sc_packet_remove_object* my_packet = reinterpret_cast<sc_packet_remove_object*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.hide();
		}
		else if (other_id < MAX_USER) {
			players[other_id].hide();
		}
		else {
			players[other_id].hide();
		}
		break;
	}

	case SC_PACKET_CHAT:
	{
		sc_packet_chat* my_packet = reinterpret_cast<sc_packet_chat*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.set_chat(my_packet->message);
		}
		else if (other_id < MAX_USER) {
			players[other_id].set_chat(my_packet->message);
		}
		else {
			players[other_id].set_chat(my_packet->message);
		}
		break;
	}
	case SC_PACKET_ATTACK:
	{
		cout << "a" << endl;
		sc_packet_attack* packet = reinterpret_cast<sc_packet_attack*>(ptr);
		cout << packet->hp << endl;
		avatar.hp = packet->hp;

		break;
	}
	
	default:
		printf("Unknown PACKET type [%d]\n", ptr[1]);
	}
	
}

void process_data(char* net_buf, size_t io_byte)
{
	char* ptr = net_buf;
	static size_t in_packet_size = 0;
	static size_t saved_packet_size = 0;
	static char packet_buffer[BUF_SIZE];

	while (0 != io_byte) {
		if (0 == in_packet_size) in_packet_size = ptr[0];
		if (io_byte + saved_packet_size >= in_packet_size) {
			memcpy(packet_buffer + saved_packet_size, ptr, in_packet_size - saved_packet_size);
			ProcessPacket(packet_buffer);
			ptr += in_packet_size - saved_packet_size;
			io_byte -= in_packet_size - saved_packet_size;
			in_packet_size = 0;
			saved_packet_size = 0;
		}
		else {
			memcpy(packet_buffer + saved_packet_size, ptr, io_byte);
			saved_packet_size += io_byte;
			io_byte = 0;
		}
	}
}

bool client_main()
{
	char net_buf[BUF_SIZE];
	size_t	received;

 	auto recv_result = socket.receive(net_buf, BUF_SIZE, received);
	if (recv_result == sf::Socket::Error)
	{
		wcout << L"Recv 에러!";
		while (true);
	}
	if (recv_result == sf::Socket::Disconnected)
	{
		wcout << L"서버 접속 종료.\n";
		return false;
	}
	if (recv_result != sf::Socket::NotReady)
		if (received > 0) process_data(net_buf, received);

	for (int i = 0; i < SCREEN_WIDTH; ++i)
		for (int j = 0; j < SCREEN_HEIGHT; ++j)
		{
			int tile_x = i + g_x_origin;
			int tile_y = j + g_y_origin;
			if ((tile_x < 0) || (tile_y < 0)) continue;
			/*if (obs[tile_x * i + 7][tile_y * j + 7] == 1)
			{
				obstacle_tile.a_move(TILE_WIDTH * i + 7, TILE_WIDTH * j + 7);
				obstacle_tile.a_draw();
			}*/
			if ((((tile_x / 3) + (tile_y / 3)) % 2) == 1) {
				white_tile.a_move(TILE_WIDTH * i + 7, TILE_WIDTH * j + 7);
				white_tile.a_draw();
			}
			else
			{
				black_tile.a_move(TILE_WIDTH * i + 7, TILE_WIDTH * j + 7);
				black_tile.a_draw();
			}
		}
	for (int i = -10; i < 10; ++i)
	{
		for (int j = -10; j < 10; ++j)
		{
			if (obs[avatar.m_y + i][avatar.m_x + j] == 1)
			{
				obstacle_tile.a_move(TILE_WIDTH * (j+10) + 7, TILE_WIDTH * (i+10) + 7);
				obstacle_tile.a_draw();
			}
		}
	}

	avatar.draw();
	for (auto& pl : players) pl.draw();
	sf::Text text;
	text.setFont(g_font);
	char buf[100];
	sprintf_s(buf, "(%d, %d)", avatar.m_x, avatar.m_y);
	text.setString(buf);

	sf::RectangleShape HP(sf::Vector2f(avatar.hp / avatar.maxhp * 100,20));
	HP.setPosition(570, 1250);
	HP.setFillColor(sf::Color(255, 0, 0, 255));

	sf::RectangleShape EXP(sf::Vector2f(avatar.exp, 20));
	EXP.setPosition(570, 1300);
	EXP.setFillColor(sf::Color(255, 255, 0, 255));

	g_window->draw(text);
	//g_window->draw(HP);
	//g_window->draw(EXP);
	return true;
}

void send_move_packet(char dr)
{
	cs_packet_move packet;
	packet.size = sizeof(packet);
	packet.type = CS_PACKET_MOVE;
	packet.direction = dr;
	size_t sent = 0;
	socket.send(&packet, sizeof(packet), sent);
}
void send_attack_packet(char dr)
{
	//cout << "공격해버렷" << endl;
	cs_packet_attack packet;
	packet.size = sizeof(packet);
	packet.id = g_myid;
	packet.type = CS_PACKET_ATTACK;
	size_t sent = 0;
	socket.send(&packet, sizeof(packet), sent);
}

void send_login_packet(string& name)
{
	cout << "보내버렷" << endl;
	cs_packet_login packet;
	packet.size = sizeof(packet);
	packet.type = CS_PACKET_LOGIN;
	strcpy_s(packet.name, name.c_str());
	size_t sent = 0;
	socket.send(&packet, sizeof(packet), sent);
}

int main()
{
	wcout.imbue(locale("korean"));
	sf::Socket::Status status = socket.connect("127.0.0.1", SERVER_PORT);


	socket.setBlocking(false);

	if (status != sf::Socket::Done) {
		wcout << L"서버와 연결할 수 없습니다.\n";
		while (true);
	}

	client_initialize();

	cin >> name;
	auto tt = chrono::duration_cast<chrono::milliseconds>
		(chrono::system_clock::now().
			time_since_epoch()).count();
	//name += to_string(tt % 1000);
	send_login_packet(name);

	avatar.set_name(name.c_str());
	sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "2D CLIENT");
	g_window = &window;

	while (window.isOpen())
	{
		sf::Event event;
		
		while (window.pollEvent(event))
		{
			int direction = -1;
			if (event.type == sf::Event::Closed)
				window.close();
			if (event.type == sf::Event::KeyPressed) {
				direction = -1;
				switch (event.key.code) {
				case sf::Keyboard::Left:
					//cout << "A" << endl;
					direction = 2;
					break;
				case sf::Keyboard::Right:
					//cout << "AAB" << endl;
					direction = 3;
					break;
				case sf::Keyboard::Up:
					//cout << "AAC" << endl;
					direction = 0;
					break;
				case sf::Keyboard::Down:
					//cout << "AAD" << endl;
					direction = 1;
					break;
				case sf::Keyboard::Escape:
					window.close();
					break;
				case sf::Keyboard::A:
					//cout << "AA" << endl;
					direction = 4;
					break;
				}
				if (direction == 4) { send_attack_packet('A'); direction = -1; break; }
				else if (-1 != direction)
					send_move_packet(direction);
			}
		}

		window.clear();
		if (false == client_main())
			window.close();
		window.display();
	}
	client_finish();

	return 0;
}