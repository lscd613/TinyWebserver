#include "http_conn.h"

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;

//需要设置socket非阻塞，不然碰到无数据可读的时候就会一直阻塞
//核心函数fcntl
int setnonblocking(int fd) {
    int pre_option = fcntl(fd, F_GETFL);
    int new_option = pre_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return pre_option;
}


//注册新事件,oneshot保证一个socket只能由一个线程负责
//核心函数epoll_ctl
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;

    if(one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    //最后记得设置非阻塞
    setnonblocking(fd);
}

void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//重置EPOLLONESHOT事件
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;

    event.events = ev | EPOLLRDHUP | EPOLLONESHOT;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

bool http_conn::write() {
    ;
}

bool http_conn::read_once() {
    // //异常
    // if (m_read_idx >= READ_BUFFER_SIZE) {
    //     return false;
    // }
    // //一直读取直到没有数据可读
    // int bytes_read = 0;
    // while (true) {
    //     bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE-m_read_idx, 0);
    //     if (bytes_read == -1) {
    //         if (errno == EAGAIN || errno == EWOULDBLOCK) {
    //             break;
    //         }
    //         return false;
    //     }
    //     //对方关闭连接
    //     else if (bytes_read == 0) {
    //         return false;
    //     }
    //     m_read_idx += bytes_read;
    // }
    // return true;
    printf("写数据\n");
    return true;
}

void http_conn::init(int sockfd, const sockaddr_in &addr) {
    m_sockfd = sockfd;
    m_address = addr;

    //端口复用
    int reuse = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    //在epoll中注册
    addfd(m_epollfd, sockfd, true);
    m_user_count++;
}

void http_conn::close_conn() {
    if (m_sockfd != -1) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}