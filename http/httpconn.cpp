#include "httpconn.h"

#include <mysql/mysql.h>
#include <fstream>

// 文件描述符设置非阻塞
int setnonblocking(int fd) {
    int flag = fcntl(fd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flag);

    return fd;
}

// 内核事件表注册新事件
// 针对客户端连接的描述符要注册EPOLLONESHOT事件
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event ev;
    ev.data.fd = fd;
#ifdef ET
    // EPOLLRDHUP 对方关闭连接
    ev.event = EPOLLIN | EPOLLET | EPOLLRDHUP;  // 线程处理完socket之后要立即重置EPOLLONESHOT事件
#endif

#ifdef LT
    ev.event = EPOLLIN | EPOLLRDHUP;
#endif
    if (one_shot) {
        ev.event |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
    setnonblocking(fd);  // 设置非阻塞
}

// 重置EPOLLONESHOT事件
void modfd(int epoll, int fd, int event) {
    epoll_event ev;
    ev.data.fd = fd;
#ifdef ET
    ev.event = event | EPOLLIN | EPOLLRDHUP | EPOLLONESHOT ;
#endif

#ifdef LT
    ev.event = EPOLLRDHUP | EPOLLONESHOT;
#endif
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev)
}

// 内核事件表删除事件
void removefd(int epollfd, int fd) {
    epoll(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}



void httpconn::init(int sockfd, const sockaddr_in &addr) {
    m_sockfd = sockfd;
    m_address = addr;
    
    init();
}

bool httpconn::read_once() {
    if (m_read_idx > READ_BUFFER_SIZE) return false;  // 读缓存区满了
    int byte_read = 0;
    // 循环读数据
    while (true) {
        byte_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (byte_read == 0) {
            return false;  // 连接断开了
        } else if (byte_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;  // 读完了
            return false;
        }
        m_read_idx += byte_read;
    }
    return true;
}

// 