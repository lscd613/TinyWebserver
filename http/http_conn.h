#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <map>
#include <string>
#include "../CGImysql/sql_connection_pool.h"
#include <fcntl.h>
#include <unistd.h>



class http_conn         
{
public:
    //读取文件名字的长度和读写缓冲区长度
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    //请求方法
    enum METHOD{
        GET,
        POST
    };
    //HTTP状态码
    enum HTTP_CODE{
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    //解析请求报文的主状态
    enum CHECK_STATE{
        CHECK_STATE_REQUESTLINE,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    //解析报文的从状态（解析行的状态）
    enum LINE_STATUS{
        LINE_OK,
        LINE_BAD,
        LINE_OPEN//?
    };

public:
    http_conn(){}
    ~http_conn(){}

public:
    //初始化套接字地址,要调用私有方法init -qa
    void init(int sockfd,const sockaddr_in &addr);
    //关闭连接，real_close的作用-qa
    void close_conn();
    //处理报文 -qa
    void process();
    bool read_once();
    bool write();
    sockaddr_in* get_address(){
        return &m_address;
    }
    //-qa
    //初始化数据库读取表?
    void init_mysql_result();
    //CGI使用线程池初始化数据库表?
    void init_result_file(connection_pool *connPool);

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;

private:
    void init();
    //处理报文，从m_read_buf读取
    HTTP_CODE process_read();
    //解析请求行、头部、内容
    //参数qa
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_header(char* text);
    HTTP_CODE parse_content(char* text);
    //生成响应报文
    HTTP_CODE do_request();
    //写入响应报文,参数qa
    bool process_write(HTTP_CODE ret);

    //指针后移指向未处理的字符
    char* get_line(){return m_read_buf+m_start_line;}
    LINE_STATUS parse_line();

    //内存映射相关
    void unmap();

    //拆分生成报文，最后由do_request调用
    //第二个参数是可变参数 qa
    bool add_response(const char *format,...);
    bool add_content(const char* content);
    bool add_status_line(int status,const char* title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();
private:
    sockaddr_in m_address;
    int m_sockfd;

    //读相关
    char m_read_buf[READ_BUFFER_SIZE];
    int m_read_idx;
    int m_checked_idx;
    int m_start_line;

    //写相关
    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;

    CHECK_STATE m_check_state;
    METHOD m_method;

    //请求报文的信息
    char m_real_file[FILENAME_LEN];
    char *m_url;
    char *m_version;
    char *m_host;
    int m_content_length;
    bool m_linger;

    char *m_file_address;
    struct stat m_file_stat;
    struct iovec m_iv[2];
    int m_iv_count;
    int cgi;
    char *m_string;
    int bytes_to_send;
    int bytes_have_send;
};

#endif