#include "./Locker/Locker.h"
#include "./Threadpool/Threadpool.h"
#include "./http/http_conn.h"
#include <cstdio>
#include <signal.h>
#include <string.h>
//对信号处理进行封装
void addsig(int sig, void (handler)(int)) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);    //设置临时信号集，都是阻塞的
    sigaction(sig, &sa, NULL);
}

int main(int argc, char *argv[]){
    if(argc<=1){
        //需要传入端口

    }
    int port = atoi(argv[1]);

    //对SIGPIPE信号进行处理，会出现客户端断开连接而服务器还在写数据的情况
    //需要对此进行处理
    addsig(SIGPIPE, SIG_IGN);

    ////创建数据库连接池
    // connection_pool *connPool = connection_pool::GetInstance();
    // connPool->init("","","","",9006,8);

    // //初始化线程池
    // Threadpool<http_conn> *pool = NULL;
    // try {
    //     pool = new Threadpool<http_conn>(connPool);
    // } catch (...) {
    //     return 1;
    // }

    return 0;
}