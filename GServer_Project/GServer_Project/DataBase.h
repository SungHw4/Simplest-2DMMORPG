#pragma once
#include "stdafx.h"
#include <sqlext.h>
#include "CLIENT.h"
#include "SStruct.h"

// -----------------------------------------------------------------------
// DataBase.h
//   DB 연결/조회/업데이트 함수 선언.
//   기존 extern 글로벌 변수(p_x, p_y, ...) 를 제거하고
//   DBPlayerData 구조체 반환 방식으로 통일한다.
// -----------------------------------------------------------------------

void Initialize_DB();
void Disconnect_DB();

// 플레이어 로드: 결과를 DBPlayerData 로 반환 (스레드 안전)
bool Load_DB(const char* name, DBPlayerData& outData);

// 플레이어 업데이트/등록
void UpdatePlayerOnDB(int c_id, CLIENT& client);
bool Add_DB(char* ID, char* pwd);

// SQL 인젝션 필터
bool DB_Injection(std::string word);

// ODBC 진단 출력
void HandleDiagnosticRecord(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode);
