#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_
#include <mysql/mysql.h>
#include <string>
#include "../Locker/Locker.h"
#include <list>
using namespace std;
class connection_pool
{
   
public:
    static connection_pool *GetInstance();
    void init(string url, string User, string PassWord
    , string DataBaseName, int Port, unsigned int MaxConn);
    connection_pool(/* args */);
    ~connection_pool();
    MYSQL *GetConnection();             //获取数据库连接
    bool ReleaseConnection(MYSQL* conn);//释放连接
    
    int GetFreeConn(); //获取连接
    void DestroyPool();//销毁所有连接

private:
    unsigned int MaxConn;
    unsigned int CurConn;
    unsigned int FreeConn;

    Locker locker;
    Sem reserve;
    list<MYSQL*> connList;

    string url;
    string Port;
    string User;
    string Password;
    string DatabaseName;
};



class connectionRAII
{
private:
    MYSQL *conRAII;
    connection_pool *poolRAII;
public:
    connectionRAII(MYSQL **con,connection_pool *connPool);
    ~connectionRAII();
};



#endif
