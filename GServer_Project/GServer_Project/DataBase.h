#pragma once
#include "stdafx.h"
#include <sqlext.h>  
#include "CLIENT.h"

SQLHENV henv;
SQLHDBC hdbc;
SQLHSTMT hstmt = 0;
SQLINTEGER p_id;

SQLINTEGER p_x;
SQLINTEGER p_y;
SQLINTEGER p_hp;
SQLINTEGER p_maxhp;
SQLINTEGER p_exp;
SQLINTEGER p_lv;

void Initialize_DB();
void Disconnect_DB();
void Update_DB(char* ID, char* pwd);
bool Add_DB(char* ID, char* pwd);
bool Load_DB(string t);
bool Load_DB(char* t);
void UpdatePlayerOnDB(int c_id, CLIENT& client);
bool DB_Injection(std::string word);
void HandleDiagnosticRecord(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode);
