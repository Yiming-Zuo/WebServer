#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <libgen.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

#include "lock/locker.h"
#include "threadpool/threadpool.h"
#include "http/http_conn.h"

#define MAX_FD 65536 // 最大文件描述符
#define MAX_EVENT_NUM 10000  // 最大事件数

#define LT
// #define ET

extern void addfd(int epollfd, int fd, bool one_shot);
extern int removefd(int epollfd, int fd);

// 打印并且向客户端发送错误
void show_error(int connfd, const char *info) 
{
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int main(int argc, char *argv[])
{
    if (argc <= 2)
    {   
        printf("usage: %s ip_address port_number\n", basename(argv[0]));  // basename(): 获取路径中的文件名
        return 1;
    }
    const char *ip = argv[1];
    int port = atoi(argv[2]);

    // 忽略SIGPIPE信号
    signal(SIGPIPE, SIG_IGN);

    // 创建线程池
    threadpool<http_conn> *pool = NULL;
    try
    {
        pool = new threadpool<http_conn>;
    }
    catch(...)
    {
        return 1;
    }
    
    // 创建http_conn类对象
    http_conn *users = new http_conn[MAX_FD];  // TODO
    assert(users != NULL);  // TODO
    int user_count = 0;  // TODO

    // lfd
    int lfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(lfd >= 0);
    struct linger tmp = {1, 0};  // tcp强制断开连接，close()立即返回，不会发送未发送完的数据，通过发送一个RST包强制关闭socket描述符。
    setsockopt(lfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    // bind
    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));  // 将结构体空间清零
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    // inet_pton(AF_INET, ip, &addr.sin_addr.s_addr);
    addr.sin_addr.s_addr = INADDR_ANY;
    int ret  = bind(lfd, (struct sockaddr *)&addr, sizeof(addr));
    assert(ret >= 0);

    // listen
    ret = listen(lfd, 100);
    assert(ret >= 0);
    printf("等待连接\n");

    // 创建内核事件表
    epoll_event evs[MAX_EVENT_NUM];
    // 构造epoll树
    int epollfd = epoll_create(10);
    assert(epollfd != -1);
    // 将lfd挂到epoll树上
    addfd(epollfd, lfd, false);
    // epollfd复制给http_conn类的静态成员变量
    http_conn::m_epollfd = epollfd;

    while (true) 
    {
        // 监听epoll上文件描述符的事件
        int num = epoll_wait(epollfd, evs, MAX_EVENT_NUM, -1);
        if (num < 0 && errno != EINTR) 
        {
            perror("epoll");
            break;
        }
        // 遍历就绪事件
        for (int i = 0; i < num; i++) 
        {
            int sockfd = evs[i].data.fd;
            // 监听到新的客户端连接
            if (sockfd == lfd) 
            {
                struct sockaddr_in addr_cli;
                socklen_t addr_len = sizeof(addr_cli);
#ifdef LT
                // accept
                int connfd = accept(lfd, (struct sockaddr *)&addr_cli, &addr_len);
                if (connfd < 0) 
                {
                    perror("accept");
                    continue;
                }
                char str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &addr.sin_addr.s_addr, str, sizeof(str));
                printf("%s 连接成功\n", str);
                if (http_conn::m_user_count >= MAX_FD)  // socket连接数 
                {
                    show_error(connfd, "Internal server busy");
                    continue;
                }
                users[connfd].init(connfd, addr_cli);  // 创建http处理对象
#endif

#ifdef ET
                // 循环读数据
                while (1)
                {
                    // accept
                    int connfd = accept(lfd, (struct sockaddr *)&addr_cli, &addr_len);
                    if (connfd < 0) 
                    {
                        perror("accept");
                        break;
                    }
                    if (http_conn::m_user_count >= MAX_FD)  // socket连接数 
                    {
                        show_error(connfd, "Internal server busy");
                        break;
                    }
                    users[connfd].init(connfd, addr_cli);  // 创建http处理对象
                }
                continue;
#endif          
            }
            // 处理异常事件 
            else if (evs[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) 
            {  
                // 服务器端关闭连接
                users[sockfd].close_conn();
            }
            // 读事件
            else if (evs[i].events & EPOLLIN)
            {
                // 读入对应缓冲区
                if (users[sockfd].read_once()) {
                    // 将请求加入队列
                    pool->append(users + sockfd);
                }
                else {
                    users[sockfd].close_conn();
                }
            }
            // 写事件
            else if (evs[i].events & EPOLLOUT) {
                if (!users[sockfd].write()) {
                    users[sockfd].close_conn();
                }
            }
            else {
                users[sockfd].close_conn();
            }
        }
    }
    close(epollfd);
    close(lfd);
    delete[] users;
    delete pool;
    return 0;
}