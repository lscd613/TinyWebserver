#include "sql_connection_pool.h"
#include <iostream>
using namespace std;
connection_pool::connection_pool()
{
	this->CurConn = 0;
	this->FreeConn = 0;
}


connection_pool *connection_pool::GetInstance() {
    static connection_pool connPool;
    return &connPool;
}

connection_pool::~connection_pool()
{
    DestroyPool();
}


void connection_pool::init(string url, string User, string PassWord, 
string DataBaseName, int Port, unsigned int MaxConn) {
    this->url = url;
    this->User = User;
    this->Password = PassWord;
    this->DatabaseName = DataBaseName;
    this->Port = Port;

    locker.lock();
    for (int i = 0; i < MaxConn; i++) {
        MYSQL *con = NULL;
        con = mysql_init(con);
        if (con == NULL) {
            cout << "Error:" << mysql_error(con);
            exit(1);
        }

        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), 
        DatabaseName.c_str(), Port, NULL, 0);

        if (con == NULL) {
            cout << "Error:" << mysql_error(con);
            exit(1);
        }
        connList.push_back(con);
        ++FreeConn;
    }
    reserve = Sem(FreeConn);

    this->MaxConn = FreeConn;
    locker.unlock();
}

MYSQL* connection_pool::GetConnection() {
    //异常处理
    if (0 == connList.size()) return NULL;

    MYSQL *con = NULL;
    //P操作
    reserve.wait();
    //有资源之后上锁
    locker.lock();
    con = connList.front();
    connList.pop_front();

    --FreeConn;
    ++CurConn;

    return con;
}
bool connection_pool::ReleaseConnection(MYSQL* conn) {
    //异常
    if (conn == NULL) return false;
    locker.lock();
    connList.push_back(conn);
    ++FreeConn;
    --CurConn;
    locker.unlock();
    
    reserve.post();
    return true; 
}
    
int connection_pool::GetFreeConn() {
    return this->FreeConn;
}
void connection_pool::DestroyPool() {
    locker.lock();
    if (connList.size() > 0) {
        for (list<MYSQL *>::iterator it = connList.begin(); it != connList.end(); it++)
        {
            MYSQL *conn = *it;
            mysql_close(conn);
        }
        CurConn = 0;
        FreeConn = 0;
        connList.clear();
    }
    locker.unlock();
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool) {
    *SQL = connPool->GetConnection();
    
    conRAII = *SQL;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII() {
    poolRAII->ReleaseConnection(conRAII);
}