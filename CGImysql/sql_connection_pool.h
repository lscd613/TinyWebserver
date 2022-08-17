#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_
#include <mysql/mysql.h>
class connection_pool
{
private:
    MYSQL *GetConnection();             //获取数据库连接
    bool ReleaseConnection(MYSQL* conn);//释放连接
public:
    connection_pool(/* args */);
    ~connection_pool();
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
