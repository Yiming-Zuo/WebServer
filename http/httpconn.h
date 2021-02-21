#ifndef HTTPCONN_H
#define HTTPCONN_H

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

class httpconn {
public:
    static const int FLIENAME_LEN = 200;
    static const int READ_BUFFER_SIZE =2048;
    static const int WRITE_BUFFER_SIZE = 1024;

    // 初始化套接字地址，内部调用私有方法init
    void init(int sockfd, const sockaddr_in &addr);
    // 读取浏览器发来的全部数据
    bool read_once();

private:
    void init();

public:
    

private:
    int m_sockfd;
    sockaddr_in m_address;

    // 读缓存区
    char m_read_buf[READ_BUFFER_SIZE];
    // 读缓存区中数据最后一个字节 + 1
    int m_read_idx;
};


#endif // !HTTPCONN_H
