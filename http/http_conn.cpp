#include "http_conn.h"
#include <string.h>
#include <sys/mman.h>
#include <sys/uio.h>
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

const char* doc_root="/home/lighthouse/FayeRingo/TinyWebserver/root";

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
    printf("%s\n",m_read_buf);
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
    if (!write_ret) {
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
        case CHECK_STATE_HEADER:
        {
            ret = parse_header(text);
            if (ret == BAD_REQUEST) {
                return BAD_REQUEST;
            }
            else if (ret==GET_REQUEST){
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            ret = parse_content(text);
            if (ret==GET_REQUEST){
                return do_request();
            }
            line_status = LINE_OPEN;
            break;
        }    
        default:
            return INTERNAL_ERROR;
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
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
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
http_conn::HTTP_CODE http_conn::parse_header(char* text) {
    if (text[0] == '\0') {
        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        printf("GET_REQUEST\n");
        return GET_REQUEST;
    }
    else if(strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text," \t");
        if(strcasecmp(text,"keep-alive")==0)
        {
            //qa
            //如果是长连接，则将linger标志设置为true
            m_linger=true;
        }
    }
    else if(strncasecmp(text,"Content-length:",15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if(strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text," \t");
        m_host = text;
    }
    else {
        printf("unknow header: %s\n", text);
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char* text) {
    //判断buf中是否读取消息体
    if(m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        //POST请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request(){
    //初始化m_real_file为网站根目录
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    const char *p = strrchr(m_url, '/');

    //实现登录和注册校验
    if (cgi == 1 && (*(p+1) == '2' || *(p+1) == '3')) {
        //判断是注册或登录校验

        //同步线程登录校验

        //CGI多进程登录校验
    }
    //如果请求资源为/0，跳转注册界面
    if (*(p+1) == '0') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");

        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    //如果请求资源为/1，表示跳转登录界面
    else if (*(p+1) == '1') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");

        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else
    //如果以上均不符合，即不是登录和注册，直接将url与网站目录拼接   
    //这里的情况是welcome界面，请求服务器上的一个图片
        strncpy(m_real_file+len,m_url,FILENAME_LEN-len-1);
    //获取请求文件信息
    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;
    //判断文件权限
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;
    //判断文件类型，如果是目录则表示请求报文有误
    if (S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }

    //将文件映射到内存中
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    close(fd);
    return FILE_REQUEST;
}

bool http_conn::add_response(const char* format,...) {
    //异常
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;
    va_start (arg_list,format);

    int len=vsnprintf(m_write_buf+m_write_idx,WRITE_BUFFER_SIZE-1-m_write_idx,format,arg_list);

    if (len >= WRITE_BUFFER_SIZE-1-m_write_idx) {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

//添加状态行
bool http_conn::add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n","HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_linger();
    add_blank_line();
}
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}


bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret)
    {
        //内部错误，500
        case INTERNAL_ERROR:{
            add_status_line(500,error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form))
                return false;
            break;
        }
        //报文语法有误，404
        case BAD_REQUEST:{
            add_status_line(404,error_404_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form))
                return false;
            break;
        }
        //资源无访问权限，403
        case FORBIDDEN_REQUEST: {
            add_status_line(403,error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form))
                return false;
            break;
        }
        //文件存在，200
        case FILE_REQUEST:{
            add_status_line(200,ok_200_title);
            
            if (m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;

                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            }
            else {
                const char* ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if(!add_content(ok_string))
                    return false;
            }
        }
        default:
            return false;
    }
    //除FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

void http_conn::unmap(){
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

bool http_conn::write() {
    int temp = 0;
    int newadd = 0;
    if(bytes_to_send == 0){
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        init();
        return true;
    }
    while(1) {
        temp = writev(m_sockfd,m_iv,m_iv_count);
        if(temp > 0) {
            bytes_have_send = temp;
            newadd = bytes_have_send - m_write_idx;
        }
        if (temp <= -1) {
            //缓冲区已满
            if(errno == EAGAIN) {
                if(bytes_have_send >= m_iv[0].iov_len) {
                    m_iv[0].iov_len = 0;
                    m_iv[1].iov_base = m_file_address + newadd;
                    m_iv[1].iov_len = bytes_to_send;
                }
                else {
                    //qa
                    m_iv[0].iov_base = m_write_buf + bytes_to_send;
                    m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
                }
                modfd(m_epollfd,m_sockfd,EPOLLOUT);
                return true;
            }
            //不是缓冲区问题，取消映射
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        if(bytes_to_send <= 0) {
            unmap();
            //需要重置oneshot事件
            modfd(m_epollfd,m_sockfd,EPOLLIN);
            if(m_linger){
                init();
                return true;
            }
            else
                return false;
        }
    }
}

