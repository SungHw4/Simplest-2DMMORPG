#include "DataBase.h"

// -----------------------------------------------------------------------
// ODBC 핸들 (이 파일 내부에서만 사용 - 전역 공유 X)
// GameDBService 는 단일 스레드에서만 DB 함수를 호출하므로
// hdbc 공유는 현재 안전하다. 향후 멀티 DB 스레드 시 커넥션 풀 필요.
// -----------------------------------------------------------------------
static SQLHENV henv = nullptr;
static SQLHDBC hdbc = nullptr;

// -----------------------------------------------------------------------
// 내부 유틸
// -----------------------------------------------------------------------
static void show_error()
{
    printf("DB error\n");
}

void HandleDiagnosticRecord(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode)
{
    SQLSMALLINT iRec = 0;
    SQLINTEGER  iError;
    WCHAR wszMessage[1000];
    WCHAR wszState[SQL_SQLSTATE_SIZE + 1];
    if (RetCode == SQL_INVALID_HANDLE) {
        fwprintf(stderr, L"Invalid handle!\n");
        return;
    }
    while (SQLGetDiagRec(hType, hHandle, ++iRec, wszState, &iError, wszMessage,
        (SQLSMALLINT)(sizeof(wszMessage) / sizeof(WCHAR)), (SQLSMALLINT*)NULL) == SQL_SUCCESS)
    {
        if (wcsncmp(wszState, L"01004", 5)) {
            fwprintf(stderr, L"[%5.5s] %s (%d)\n", wszState, wszMessage, iError);
        }
    }
}

// -----------------------------------------------------------------------
// Initialize_DB
// -----------------------------------------------------------------------
void Initialize_DB()
{
    SQLRETURN retcode;
    setlocale(LC_ALL, "korean");

    retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) return;

    retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);
    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) return;

    retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) return;

    SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);

    retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2021_GServer_ODBC", SQL_NTS,
                         (SQLWCHAR*)NULL, 0, NULL, 0);
    if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
        cout << "DB connect SUCCESS" << endl;
    else
        cout << "DB connect FAILED" << endl;
}

// -----------------------------------------------------------------------
// Disconnect_DB
// -----------------------------------------------------------------------
void Disconnect_DB()
{
    SQLDisconnect(hdbc);
    SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
    SQLFreeHandle(SQL_HANDLE_ENV, henv);
    hdbc = nullptr;
    henv = nullptr;
}

// -----------------------------------------------------------------------
// Load_DB
//   플레이어 이름으로 DB를 조회하고 결과를 outData 에 채운다.
//   기존 extern 전역 변수(p_x, p_y, ...) 방식을 대체한다.
//   함수 내부에서 로컬 hstmt를 할당/해제하므로 스레드 안전.
// -----------------------------------------------------------------------
bool Load_DB(const char* name, DBPlayerData& outData)
{
    if (!name || name[0] == '\0') return false;

    // SQL Injection 방지: 허용되지 않은 문자가 포함된 이름은 거부
    if (!DB_Injection(name)) return false;

    SQLHSTMT  localStmt = nullptr;
    SQLRETURN retcode;

    // 로컬 statement 핸들 할당
    retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &localStmt);
    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) return false;

    // 쿼리 문자열 생성
    char tmp[128];
    sprintf_s(tmp, sizeof(tmp), "EXEC LoadPlayer '%s'", name);

    wchar_t* exec = nullptr;
    int strSize = MultiByteToWideChar(CP_ACP, 0, tmp, -1, NULL, NULL);
    exec = new WCHAR[strSize];
    MultiByteToWideChar(CP_ACP, 0, tmp, -1, exec, strSize);

    retcode = SQLExecDirect(localStmt, (SQLWCHAR*)exec, SQL_NTS);
    delete[] exec;

    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO)
    {
        SQLFreeHandle(SQL_HANDLE_STMT, localStmt);
        return false;
    }

    // 결과 바인딩 — 로컬 변수에 직접 바인딩 (전역 변수 없음)
    SQLWCHAR  p_Name[NAME_LEN] = {};
    SQLINTEGER p_x = 0, p_y = 0, p_lv = 0, p_hp = 0, p_maxhp = 0, p_exp = 0;
    SQLLEN cbName = 0, cbX = 0, cbY = 0, cbLv = 0, cbHp = 0, cbMaxhp = 0, cbExp = 0;

    SQLBindCol(localStmt, 1, SQL_C_WCHAR,  p_Name, sizeof(p_Name), &cbName);
    SQLBindCol(localStmt, 2, SQL_C_LONG,  &p_x,    sizeof(p_x),    &cbX);
    SQLBindCol(localStmt, 3, SQL_C_LONG,  &p_y,    sizeof(p_y),    &cbY);
    SQLBindCol(localStmt, 4, SQL_C_LONG,  &p_lv,   sizeof(p_lv),   &cbLv);
    SQLBindCol(localStmt, 5, SQL_C_LONG,  &p_hp,   sizeof(p_hp),   &cbHp);
    SQLBindCol(localStmt, 6, SQL_C_LONG,  &p_maxhp,sizeof(p_maxhp),&cbMaxhp);
    SQLBindCol(localStmt, 7, SQL_C_LONG,  &p_exp,  sizeof(p_exp),  &cbExp);

    retcode = SQLFetch(localStmt);
    if (retcode == SQL_ERROR || retcode == SQL_SUCCESS_WITH_INFO)
        show_error();

    bool success = (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO);

    if (success)
    {
        outData.success = true;
        outData.x       = static_cast<int>(p_x);
        outData.y       = static_cast<int>(p_y);
        outData.level   = static_cast<int>(p_lv);
        outData.hp      = static_cast<int>(p_hp);
        outData.maxhp   = static_cast<int>(p_maxhp);
        outData.exp     = static_cast<int>(p_exp);
        // 이름을 char 배열로 변환
        WideCharToMultiByte(CP_ACP, 0, p_Name, -1,
                            outData.name, sizeof(outData.name), NULL, NULL);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, localStmt);
    return success;
}

// -----------------------------------------------------------------------
// UpdatePlayerOnDB
// -----------------------------------------------------------------------
void UpdatePlayerOnDB(int /*c_id*/, Player& client)
{
    // SQL Injection 방지: DB에서 로드된 이름이라도 검증
    if (!DB_Injection(client.name)) return;

    SQLHSTMT  localStmt = nullptr;
    SQLRETURN retcode;

    retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &localStmt);
    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) return;

    char tmp[128];
    sprintf_s(tmp, sizeof(tmp),
              "EXEC UpdatePlayer '%s', %d, %d, %d, %d, %d",
              client.name, (int)client.level,
              (int)client.x.load(), (int)client.y.load(),
              (int)client.hp.load(), (int)client.exp);

    wchar_t* exec = nullptr;
    int strSize = MultiByteToWideChar(CP_ACP, 0, tmp, -1, NULL, NULL);
    exec = new WCHAR[strSize];
    MultiByteToWideChar(CP_ACP, 0, tmp, -1, exec, strSize);

    SQLExecDirect(localStmt, (SQLWCHAR*)exec, SQL_NTS);

    delete[] exec;
    SQLFreeHandle(SQL_HANDLE_STMT, localStmt);
}

// -----------------------------------------------------------------------
// Add_DB
// -----------------------------------------------------------------------
bool Add_DB(char* ID, char* pwd)
{
    if (!DB_Injection(ID) || !DB_Injection(pwd))
        return false;

    SQLHSTMT  localStmt = nullptr;
    SQLRETURN retcode;

    retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &localStmt);
    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) return false;

    char tmp[128];
    sprintf_s(tmp, sizeof(tmp), "EXEC ADDPlayer '%s', '%s'", ID, pwd);

    wchar_t* exec = nullptr;
    int strSize = MultiByteToWideChar(CP_ACP, 0, tmp, -1, NULL, NULL);
    exec = new WCHAR[strSize];
    MultiByteToWideChar(CP_ACP, 0, tmp, -1, exec, strSize);

    retcode = SQLExecDirect(localStmt, (SQLWCHAR*)exec, SQL_NTS);
    delete[] exec;
    SQLFreeHandle(SQL_HANDLE_STMT, localStmt);

    return (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO);
}

// -----------------------------------------------------------------------
// DB_Injection
// -----------------------------------------------------------------------
bool DB_Injection(std::string word)
{
    const std::string allowableCharacters = "abcdefghijklmnopqrstuvwxyz0123456789!^";
    for (char ch : word)
    {
        if (allowableCharacters.find(ch) == std::string::npos)
        {
            cout << "can't using word" << endl;
            return false;
        }
    }
    return true;
}
