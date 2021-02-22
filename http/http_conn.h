#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"

class http_conn 
{
public:
    http_conn();
    ~http_conn();

public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE =2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    enum METHOD { GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH };
    // 主状态机：标志解析位置
    // 解析请求行 解析请求头 解析消息体(POST)
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    // HTTP请求的处理结果
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    // 从状态机：标志解析一行的读取状态
    // 完整读取一行 报文语法有误 读取的行不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };  

    // 初始化套接字地址，内部调用私有方法init
    void init(int sockfd, const sockaddr_in &addr);
    // 读取浏览器发来的全部数据
    bool read_once();  // 读取数据
    void process();  // 处理请求
    bool write();  // 根据报文响应返回值，子线程调用向写缓存区中写入响应报文
    // 服务器端关闭连接
    void close_conn(bool read_close = true);

private:
    void init();
    HTTP_CODE process_read();  // 报文解析
    bool process_write(HTTP_CODE ret);  // 报文响应

    HTTP_CODE parse_request_line(char *text);  // 解析请求行
    HTTP_CODE parse_request_headers(char *text);  // 解析请求头
    HTTP_CODE parse_content(char *text);  // 判断http请求是否被完整读入
    HTTP_CODE do_request();  // 报文响应
    char* get_line() {return m_read_buf + m_start_line;}  // 读取一行
    LINE_STATUS parse_line();

    // 发送响应报文部分
    void unmap();
    bool add_response(const char* format, ...);  // 更新写缓存区中的报文
    bool add_content(const char* content);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);  // 添加响应头
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;  // epoll树
    static int m_user_count;  // socket连接数量

private:
    int m_sockfd;
    sockaddr_in m_address;

    // 读缓存区
    char m_read_buf[READ_BUFFER_SIZE];
    // 读缓存区中数据最后一个字节 + 1
    int m_read_idx;
    int m_checked_idx;  // 从状态机在buf中的读取位置，从状态机正在分析的字节
    int m_start_line;  // 行在buf中的起始位置
    char m_write_buf[WRITE_BUFFER_SIZE];  // 写缓存区
    int m_write_idx;

    char m_real_file[FILENAME_LEN];
    char* m_url;  // 目标URL
    char* m_version;  // HTTP版本号
    char* m_host;  // 请求头HOST字段
    int m_content_length;  // 请求头内容长度字段
    bool m_linger;  // 请求头连接字段

    CHECK_STATE m_check_state;  // 标志解析位置
    METHOD m_method;  // 请求方式

    // 报文响应部分
    char *m_file_address;  // 请求文件的地址
    struct stat m_file_stat;  // 请求文件属性
    struct iovec m_iv[2];
    int m_iv_count;
};


#endif // !HTTP_CONN_H
