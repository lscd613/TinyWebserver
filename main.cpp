#include "./Locker/Locker.h"
#include "./Threadpool/Threadpool.h"
#include "./http/http_conn.h"
#include <cstdio>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#define ET
#define MAX_FD 65536   //最大文件描述符
#define MAX_EVENT_NUMBER 10000 //监听最大事件数量

extern int addfd(int epollfd, int fd, bool one_shot);
extern int removefd(int epollfd, int fd);
extern int setnonblocking(int fd);


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
        printf("请输入端口\n");
        return 1;
    }
    printf("1\n");
    int port = atoi(argv[1]);
    printf("端口号为: %d", port);
    //对SIGPIPE信号进行处理，会出现客户端断开连接而服务器还在写数据的情况
    //需要对此进行处理
    addsig(SIGPIPE, SIG_IGN);

    //创建数据库连接
    connection_pool *connPool = connection_pool::GetInstance();
    connPool->init("localhost", "root", "root", "asa", 1234, 8);

    //创建线程池
    Threadpool<http_conn> *pool = NULL;
    try {
        pool = new Threadpool<http_conn>(connPool);
    } catch (...) {
        return 1;
    }
    //保存用户信息
    http_conn *users = new http_conn[MAX_FD];
    //监听套接字
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    //服务端结束后，第三次挥手时有个等待释放时间，这个时间内端口不会被释放
    //所以需要设置端口复用
    //在bind之前设置
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    //绑定
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    bind(listenfd, (struct sockaddr*)&address, sizeof(address));

    //监听
    listen(listenfd, 5);

    //创建epoll对象，事件数组
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    while (true) {
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        //异常
        //
        if (num < 0 && errno != EINTR) {
            printf("epoll failed\n");
            break;
        }
        //循环事件数组
        for (int i = 0; i < num; i++) {
            int sockfd = events[i].data.fd;
            //有连接进来
            if (sockfd == listenfd) {
                struct sockaddr_in client_address;
                socklen_t client_address_length = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_address_length);
                //是否连接成功

                //目前连接数满了
                if (http_conn::m_user_count >= MAX_FD) {
                    close(connfd);
                    continue;
                }
                //初始化新的客户连接
                //直接用连接id作为索引
                users[connfd].init(connfd, client_address);
                printf("有连接加入\n");
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                //对方异常断开或者错误事件
                users[sockfd].close_conn();   
            }
            //读事件 
            else if (events[i].events & EPOLLIN){
                if(users[sockfd].read_once()) {
                    pool->append(users + sockfd);
                } else {
                    //读取失败
                    users[sockfd].close_conn();
                }
            }
            //写事件
            else if (events[i].events & EPOLLOUT) {
                if(!users[sockfd].write()) {
                    users[sockfd].close_conn();
                }
            }

        }  
    } 
    close(epollfd);
    close(listenfd);
    delete []users;
    delete pool;
    return 0;
}