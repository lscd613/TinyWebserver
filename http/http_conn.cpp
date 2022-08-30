#include "http_conn.h"
#include <string.h>
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
    //异常
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }
    //一直读取直到没有数据可读
    int bytes_read = 0;
    while (true) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE-m_read_idx, 0);
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return false;
        }
        //对方关闭连接
        else if (bytes_read == 0) {
            return false;
        }
        m_read_idx += bytes_read;
    }
    printf("%s",m_read_buf);
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

void http_conn::process() {
    HTTP_CODE read_ret = process_read();
    //请求不完整
    if (read_ret == NO_REQUEST) {
        //修改为读事件
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    //调用process_write完成报文响应
    bool write_ret = process_write(read_ret);
    if (write_ret) {
        close_conn();
    }
    //修改为写事件
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}

http_conn::HTTP_CODE http_conn::process_read() {
     LINE_STATUS line_status = LINE_OK;
     HTTP_CODE ret = NO_REQUEST;
     char* text = NULL;
    while((line_status = parse_line()) == LINE_OK
    || (m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK)) {
        text = get_line();
        m_start_line = m_checked_idx;
        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST) {
                return BAD_REQUEST;
            }
            break;
        }
        // case CHECK_STATE_HEADER:
        // {
        //     ret = parse_header(text);
        //     if (ret == BAD_REQUEST) {
        //         return BAD_REQUEST;
        //     }
        //     else if (ret==GET_REQUEST){
        //         return do_request();
        //     }
        //     break;
        // }
        // case CHECK_STATE_CONTENT:
        // {
        //     ret = parse_content(text);
        //     if (ret==GET_REQUEST){
        //         return do_request();
        //     }
        //     line_status = LINE_OPEN;
        //     break;
        // }    
        // default:
        //     return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for (; m_checked_idx < m_read_idx; m_checked_idx++) {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r') {
            //下一个字符就到结尾，则是接收不完整
            //要继续接收
            if (m_checked_idx + 1 == m_read_idx) {
                return LINE_OPEN;
            } 
            else if (m_read_buf[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx] = '\0';
                m_read_buf[m_checked_idx + 1] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n') {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r') {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
}
void http_conn::init() {
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_checked_idx = 0;
    m_start_line = 0;
    m_read_idx = 0;
}

bool http_conn::process_write(HTTP_CODE ret) {
    
}

http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
    //各个部分之间通过\t或空格分隔
    m_url = strpbrk(text," \t");

    if (!m_url) {
        return BAD_REQUEST;
    }
    //用于将前面的数据取出
    *m_url++='\0';

    char *method = text;
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    }
    else if (strcasecmp(method, "POST") == 0) {
        m_method = POST;
        //qa
        cgi = 1;
    }
    else
        return BAD_REQUEST;
    //跳过空格和\t字符，指向请求资源的第一个字符
    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");

    //只支持HTTP/1.1
    if (strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }
    //报文中带有http://或者https://
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if(strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    //一般是没有带，只有/或者/..等等
    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }
    //为/时，显示欢迎界面
    if (strlen(m_url) == 1) {
        strcat(m_url,"judge.html");
    }
    //处理完毕转移状态
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}


