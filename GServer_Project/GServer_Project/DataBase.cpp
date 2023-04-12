#include "DataBase.h"

void show_error() {
    printf("error\n");
}

void HandleDiagnosticRecord(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode)
{
    SQLSMALLINT iRec = 0;
    SQLINTEGER iError;
    WCHAR wszMessage[1000];
    WCHAR wszState[SQL_SQLSTATE_SIZE + 1];
    if (RetCode == SQL_INVALID_HANDLE) {
        fwprintf(stderr, L"Invalid handle!\n");
        return;
    }
    while (SQLGetDiagRec(hType, hHandle, ++iRec, wszState, &iError, wszMessage,
        (SQLSMALLINT)(sizeof(wszMessage) / sizeof(WCHAR)), (SQLSMALLINT*)NULL) == SQL_SUCCESS) {
        // Hide data truncated..
        if (wcsncmp(wszState, L"01004", 5)) {
            fwprintf(stderr, L"[%5.5s] %s (%d)\n", wszState, wszMessage, iError);
        }
    }
}

void Initialize_DB()
{
    SQLRETURN retcode;

    // Allocate environment handle  
    retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);

    // Set the ODBC version environment attribute  
    if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
        retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);

        // Allocate connection handle  
        if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
            retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);

            // Set login timeout to 5 seconds  
            if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
                SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);

                // Connect to data source  
                retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2021_GServer_ODBC", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

                // Allocate statement handle  
                if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
                    //    cout << "ODBC Connected," << endl;
                    retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
                    
                }
                else {
                    
                }
            }
        }
    }
}

bool Add_DB(char* ID, char* pwd)
{
    if (DB_Injection(ID) && DB_Injection(pwd))
    {
        SQLRETURN retcode;
        char tmp[100];
        sprintf_s(tmp, sizeof(tmp), "EXEC ADDPlayer %s, %s", ID, pwd);

        wchar_t* exec;
        int strSize = MultiByteToWideChar(CP_ACP, 0, tmp, -1, NULL, NULL);
        exec = new WCHAR[strSize];
        MultiByteToWideChar(CP_ACP, 0, tmp, sizeof(tmp) + 1, exec, strSize);

        retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
        retcode = SQLExecDirect(hstmt, (SQLWCHAR*)exec, SQL_NTS);

        if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
        {
            SQLLEN* pcrow = new SQLLEN;
            retcode = SQLRowCount(hstmt, pcrow);
            if (retcode == SQL_ERROR || retcode == SQL_SUCCESS_WITH_INFO) {
                HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
            }
        }
        else {
            return false;
        }
        return true;
    }
    
    return false;
}

void Update_DB(char* ID, char* pwd, CLIENT client)
{
    if (DB_Injection(ID) && DB_Injection(pwd))
    {

        CLIENT& CL = client;
        //CLIENT& CL = clients[c_id];
        std::string t = CL.name;

        SQLRETURN retcode;

        //char temp[100];
        char tmp[100];
        
        sprintf_s(tmp, sizeof(tmp), "EXEC UpdatePlayer %s, %s, %s, %s, %s, %s",CL.name, CL.level, CL.x, CL.y, CL.hp, CL.exp );
       
        wchar_t* exec;
        int strSize = MultiByteToWideChar(CP_ACP, 0, tmp, -1, NULL, NULL);
        exec = new WCHAR[strSize];
        MultiByteToWideChar(CP_ACP, 0, tmp, sizeof(tmp) + 1, exec, strSize);
        
        /*string temp = "EXEC UpdatePlayer @Param = " + t + ", @Param1 = " + std::to_string(CL.x) + ", @Param2 = " + to_string(CL.y) + ", @Param3 = " + to_string(CL.hp) + ", @Param4 = " + to_string(CL.maxhp) + ", @Param5 = " + to_string(CL.exp) + ", @Param6 = " + to_string(CL.level);
        cout << temp << endl;
        wstring tmp;
        tmp.assign(temp.begin(), temp.end());*/

        SQLWCHAR p_Name[NAME_LEN]{};
        SQLLEN cbName = 0, cbP_ID = 0, cbP_Level = 0, cbP_X = 0, cbP_Y = 0;

        retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
        retcode = SQLExecDirect(hstmt, (SQLWCHAR*)exec, SQL_NTS);

        /*retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
        retcode = SQLExecDirect(hstmt, (SQLWCHAR*)tmp.c_str(), SQL_NTS);*/
        
        if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {

            
            retcode = SQLBindCol(hstmt, 1, SQL_C_WCHAR, p_Name, NAME_LEN, &cbName);
            retcode = SQLBindCol(hstmt, 2, SQL_C_LONG, &p_x, 10, &cbP_Y);
            retcode = SQLBindCol(hstmt, 3, SQL_C_LONG, &p_y, 10, &cbP_X);

            
            for (int i = 0; ; i++) {
                retcode = SQLFetch(hstmt);
                if (retcode == SQL_ERROR || retcode == SQL_SUCCESS_WITH_INFO) show_error();
                else HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
                if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
                {
                    wprintf(L"%d: %s %d %d\n", i + 1, p_Name, p_x, p_y);
                }
                else
                    break;
            }
        }


    }


}


BOOL LoadDB(char* t)
{
    
    SQLRETURN retcode;
    char tmp[100];
    SQLSMALLINT p_level;
    SQLWCHAR p_Name[NAME_LEN];
    SQLLEN cbName = 0, cbP_ID = 0, cbP_Level = 0, cbP_X = 0, cbP_Y = 0;
    SQLLEN cbP_HP = 0, cbP_MAXHP = 0, cbP_EXP = 0, cbP_LV = 0;

    sprintf_s(tmp, sizeof(tmp), "EXEC LoadPlayerID %s", t );

    wchar_t* exec;
    int strSize = MultiByteToWideChar(CP_ACP, 0, tmp, -1, NULL, NULL);
    exec = new WCHAR[strSize];
    MultiByteToWideChar(CP_ACP, 0, tmp, sizeof(tmp) + 1, exec, strSize);

    retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
    retcode = SQLExecDirect(hstmt, (SQLWCHAR*)exec, SQL_NTS);

   if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {

       // Bind columns 1, 2, and 3  
       //retcode = SQLBindCol(hstmt, 1, SQL_C_LONG, &p_id, 100, &cbP_ID);
       retcode = SQLBindCol(hstmt, 1, SQL_C_WCHAR, p_Name, NAME_LEN, &cbName);
       retcode = SQLBindCol(hstmt, 2, SQL_C_LONG, &p_x, 10, &cbP_X);
       retcode = SQLBindCol(hstmt, 3, SQL_C_LONG, &p_y, 10, &cbP_Y);
       retcode = SQLBindCol(hstmt, 4, SQL_C_LONG, &p_lv, 10, &cbP_LV);
       retcode = SQLBindCol(hstmt, 5, SQL_C_LONG, &p_hp, 10, &cbP_HP);
       retcode = SQLBindCol(hstmt, 6, SQL_C_LONG, &p_maxhp, 10, &cbP_MAXHP);
       retcode = SQLBindCol(hstmt, 7, SQL_C_LONG, &p_exp, 10, &cbP_EXP);

       // Fetch and print each row of data. On an error, display a message and exit.  
       for (int i = 0; ; i++) {
           retcode = SQLFetch(hstmt);
           if (retcode == SQL_ERROR || retcode == SQL_SUCCESS_WITH_INFO) show_error();
           else HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
           if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
           {
               //replace wprintf with printf
               //%S with %ls
               //warning C4477: 'wprintf' : format string '%S' requires an argument of type 'char *'
               //but variadic argument 2 has type 'SQLWCHAR *'
               //wprintf(L"%d: %S %S %S\n", i + 1, sCustID, szName, szPhone);  
               wprintf(L"%d: %s %d %d\n", i + 1, p_Name, p_x, p_y);
           }
           else
               return false;
           break;
       }
   }

    return true;
}

void UpdatePlayerOnDB(int c_id, CLIENT& client)
{
    CLIENT& CL = client;
    //CLIENT& CL = clients[c_id];
    std::string t = CL.name;
    //string x = to_string(CL.x);

    SQLHENV henv;
    SQLHDBC hdbc;
    SQLHSTMT hstmt = 0;
    SQLRETURN retcode;
    SQLINTEGER p_id;

    SQLSMALLINT p_level;
    //string temp = "EXEC UpdatePlayer @Param = " + t + ", @Param1 = " + to_string(CL.x) + ", @Param2 = " + to_string(CL.y);
    string temp = "EXEC UpdatePlayer @Param = " + t + ", @Param1 = " + std::to_string(CL.x) + ", @Param2 = " + to_string(CL.y) + ", @Param3 = " + to_string(CL.hp) + ", @Param4 = " + to_string(CL.maxhp) + ", @Param5 = " + to_string(CL.exp) + ", @Param6 = " + to_string(CL.level);
    cout << temp << endl;
    wstring tmp;
    tmp.assign(temp.begin(), temp.end());
    

    setlocale(LC_ALL, "korean");

    // Allocate environment handle
    retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);

    // Set the ODBC version environment attribute
    if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
        retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);

        // Allocate connection handle
        if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
            retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);

            // Set login timeout to 5 seconds
            if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
                SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);

                // Connect to data source
                retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2021_GServer_ODBC", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

                // Allocate statement handle
                if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
               //     cout << "ODBC Connected," << endl;


                    // Process data
                    if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
                        SQLCancel(hstmt);
                        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
                    }

                    SQLDisconnect(hdbc);
                }

                SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
            }
        }
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
    }
}


bool DB_Injection(std::string word)
{
    const std::string allowableCharacters = "abcdefghijklmnopqrstuvwxyz0123456789!#^";

    for (char ch : word)
    {
        if (!(allowableCharacters.find(ch) != std::string::npos))
        {
            return false;
        }
        
    }
    return true;
   

}

void Disconnect_DB()
{
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    SQLDisconnect(hdbc);
    SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
    SQLFreeHandle(SQL_HANDLE_ENV, henv);
}
