#include "http_conn.h"



#define LT
// #define ET

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

const char* doc_root = "/home/ubuntu/projects/WebServer/root";



// 文件描述符设置非阻塞
int setnonblocking(int fd) 
{
    int flag = fcntl(fd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flag);

    return fd;
}

// 内核事件表注册读事件
// 针对客户端连接的描述符要注册EPOLLONESHOT事件
void addfd(int epollfd, int fd, bool one_shot) 
{
    epoll_event ev;
    ev.data.fd = fd;
#ifdef ET
    // EPOLLRDHUP 对方关闭连接
    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;  // 线程处理完socket之后要立即重置EPOLLONESHOT事件
#endif

#ifdef LT
    ev.events = EPOLLIN | EPOLLRDHUP;
#endif
    if (one_shot) {
        ev.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
    setnonblocking(fd);  // 设置非阻塞
}

// 重置EPOLLONESHOT事件
void modfd(int epollfd, int fd, int event) 
{
    epoll_event ev;
    ev.data.fd = fd;
#ifdef ET
    ev.events = event | EPOLLIN | EPOLLRDHUP | EPOLLONESHOT ;
#endif

#ifdef LT
    ev.events = event | EPOLLRDHUP | EPOLLONESHOT;
#endif
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev);
}

// 内核事件表删除事件
void removefd(int epollfd, int fd) 
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

// 初始化连接
void http_conn::init(int sockfd, const sockaddr_in &addr) 
{
    m_sockfd = sockfd;
    m_address = addr;
    addfd(m_epollfd, sockfd, true);
    m_user_count++;

    init();
}


// 初始化新接受的连接
void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE;  // 默认分析请求行状态
    m_linger = false;
    m_method = GET;  // 默认get请求
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);

}

// 关闭连接
void http_conn::close_conn(bool real_close)
{
    if (real_close && m_sockfd != -1)
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

// 一次性读完数据
bool http_conn::read_once() 
{
    if (m_read_idx >= READ_BUFFER_SIZE) return false;  // 读缓存区满了
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

// 处理请求
void http_conn::process()
{
    HTTP_CODE read_ret = process_read();  // 报文解析
    // 请求不完整：继续接受请求数据
    if (read_ret == NO_REQUEST)
    {
        // 注册监听读事件
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    // 报文响应
    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }
    // 注册监听写事件
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}

// 解析请求行
// 主状态机 CHECK_STATE_REQUESTLINE -> CHECK_STATE_HEADER
// 获得请求方法、目标URL和HTTP版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    // 在HTTP报文中，请求行用来说明请求类型,要访问的资源以及所使用的HTTP版本，其中各个部分之间通过\t或空格分隔。
    // 请求行中最先含有空格和\t任一字符的位置并返回
    m_url = strpbrk(text," \t");
    // 如果没有空格或\t，则报文格式有误
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    // 将该位置改为\0，用于将前面数据取出
    *m_url++ = '\0';
    // 取出数据，并通过与GET和POST比较，以确定请求方式
    char *method = text;
    if (strcasecmp(method,"GET") == 0)  // 忽略大小写
    {
        m_method=GET;
    } 
    else 
    {
        return BAD_REQUEST;
    }
        
    // m_url此时跳过了第一个空格或\t字符，但不知道之后是否还有
    // 将m_url向后偏移，通过查找，继续跳过空格和\t字符，指向请求资源的第一个字符
    m_url += strspn(m_url, " \t");  // 检索m_url中第一个不是空格或者\t的位置

    // 使用与判断请求方式的相同逻辑，判断HTTP版本号
    m_version = strpbrk(m_url," \t");
    if(!m_version)
    {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version," \t");
    // 仅支持HTTP/1.1
    if (strcasecmp(m_version,"HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }
        
    // 对请求资源前7个字符进行判断
    // 这里主要是有些报文的请求资源中会带有http://，这里需要对这种情况进行单独处理
    if (strncasecmp(m_url,"http://",7) == 0)
    {
        m_url+=7;
        m_url = strchr(m_url,'/');
    }
    // 同样增加https情况
    if(strncasecmp(m_url,"https://",8)==0)
    {
        m_url+=8;
        m_url=strchr(m_url,'/');
    }
    // 一般的不会带有上述两种符号，直接是单独的/或/后面带访问资源
    if (!m_url || m_url[0]!='/') {
        return BAD_REQUEST;
    }
        
    // 当url为/时，显示欢迎界面
    if(strlen(m_url) == 1) {
        strcat(m_url, "judge.html");  // 欢迎界面
    }

    // 请求行处理完毕，将主状态机转移处理请求头
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析请求头
// 空行 -> content-length==0 -> get请求 -> end
//         content-length!=0 -> post -> (CHECK_STATE_HEADER->CHECK_STATE_CONTENT)
// 请求头：
//      connection字段： keep-alive/close -> linger
//      content-length字段： 读取post请求的消息体长度 -> content_length
//      HOST字段 -> host
http_conn::HTTP_CODE http_conn::parse_request_headers(char* text)
{
    // 如果是空行
    if(text[ 0 ] == '\0')
    {
        if (m_method == HEAD)
        {
            return GET_REQUEST;  // 请求不完整
        }
        if (m_content_length != 0)  // post
        {
            m_check_state = CHECK_STATE_CONTENT;  // 跳转到消息体处理状态
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    // 请求头部连接字段
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        // 跳过空格和\t字符
        text += strspn(text, " \t");
        if ( strcasecmp(text, "keep-alive") == 0 )  // 长连接
        {
            m_linger = true;
        }
    }
    // 请求头部内容长度字段
    else if (strncasecmp(text, "Content-Length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol( text );
    }
    // 请求头HOST字段
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        printf( "oop! unknow header %s\n", text );
    }
    return NO_REQUEST;
}

// 解析完消息体后，更新line_status为LINE_OPEN, 跳出循环，完成解析任务
// 判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    //判断buffer中是否读取了消息体
    if(m_read_idx >= (m_content_length+m_checked_idx))
    {
        text[m_content_length]='\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 从状态机：分析出一行内容
http_conn::LINE_STATUS http_conn::parse_line()
{
    char tmp;
    // 遍历读缓冲区中还没被解析的字节
    for (; m_checked_idx < m_read_idx; m_checked_idx++)
    {
        tmp = m_read_buf[m_checked_idx];

        if (tmp == '\r') 
        {
            // \r在缓冲区末尾，说明接受不完整，要继续接受
            if ((m_checked_idx + 1) == m_read_idx) 
            {
                return LINE_OPEN;
            }
            else if (m_read_buf[m_checked_idx + 1] == '\n')  // 完整一行
            {
                m_read_buf[m_checked_idx] = '\0';
                m_read_buf[m_checked_idx + 1] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;  // 语法错误
        }

        if (tmp == '\n')
        {
            // 如果前一个字符是\r, 有可能是上次读取到\r就到buf末尾了
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx-1] == '\r')
            {
                m_read_buf[m_checked_idx] = '\0';
                m_read_buf[m_checked_idx + 1] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;  // 没有找到\r\n 继续接收
}

// 响应报文
http_conn::HTTP_CODE http_conn::do_request() 
{
    strcpy(m_real_file, doc_root);  // m_real_file初始化为网站根目录
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);  // 拼接成最终的请求文件地址
    if (stat(m_real_file, &m_file_stat) < 0)  // 获取请求文件属性
    {
        return NO_RESOURCE;  // 请求资源不存在
    }
    if (!(m_file_stat.st_mode & S_IROTH))  // S_IROTH 所有人可都
    {
        return BAD_REQUEST;  // 请求资源进制访问，没有读取权限
    }
    if (S_ISDIR(m_file_stat.st_mode))  // 请求文件为目录
    {
        return BAD_REQUEST;
    }
    
    int fd = open(m_real_file, O_RDONLY);  // 打开文件
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);  // 将文件映射到共享内存中，提高文件访问速度
    close(fd);
    return FILE_REQUEST;  // 正常访问
}

// 更新写缓存区中的报文
bool http_conn::add_response( const char* format, ... )
{
    if(m_write_idx >= WRITE_BUFFER_SIZE)  // 写入内容超过缓存区容量
    {
        return false;
    }
    va_list arg_list;  // 定义可变参数列表 指向当前参数的一个指针
    va_start(arg_list, format);  // 将arg_list初始化为传入参数 指向第一个参数
    // 将可变参数格式化的输出到一个字符数组中
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);  // 将数据format从可变参数列表写入缓冲区写，返回写入数据的长度
    if(len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))  // 写入数据长度超过缓冲区剩余容量
    {
        return false;
    }
    m_write_idx += len;  // 更新位置
    va_end(arg_list);  // 清空可变参数列表 释放指针
    return true;
}

// 响应报文添加状态行：http/1.1 状态码 状态消息
bool http_conn::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

// 消息报头添加文本长度
bool http_conn::add_content_length( int content_len )
{
    return add_response("Content-Length: %d\r\n", content_len);
}

// 消息报头添加连接状态
bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

// 消息报头添加空行
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

// 添加消息报头：文本长度、连接状态、空行
bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_linger();
    add_blank_line();
    return true;
}

// 添加content
bool http_conn::add_content(const char* content)
{
    return add_response("%s", content);
}

// 向写缓存区写入报文
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
        case INTERNAL_ERROR:  // 内部错误 500
        {
            add_status_line(500, error_500_title);  // 状态行
            add_headers(strlen(error_500_form));  // 消息报头
            if (!add_content(error_500_form))
            {
                return false;
            }
            break;
        }
        case BAD_REQUEST:  // 报文语法有误 400
        {
            add_status_line(400, error_400_title);
            add_headers(strlen( error_400_form));
            if (!add_content( error_400_form))
            {
                return false;
            }
            break;
        }
        case NO_RESOURCE:  // 请求资源不存在 404
        {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form))
            {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:  // 没有访问权限 403
        {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form))
            {
                return false;
            }
            break;
        }
        case FILE_REQUEST:  // 文件存在 200
        {
            add_status_line(200, ok_200_title);
            if (m_file_stat.st_size != 0)
            {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;  // 第一个iovec指向响应报文缓冲区
                m_iv[0].iov_len = m_write_idx;  // 响应报文长度
                m_iv[1].iov_base = m_file_address;  // 第二个iovec指向共享内存区
                m_iv[1].iov_len = m_file_stat.st_size;  // 文件大小
                m_iv_count = 2;
                return true;
            }
            else  
            {
                // 请求资源大小为0，返回空白html文件
                const char* ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string))
                {
                    return false;
                }
            }
        }
        default:
        {
            return false;
        }
    }
    // 除FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

// 取消内存映射
void http_conn::unmap()
{
    if(m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

// 发送响应报文
bool http_conn::write()
{
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    int newadd = 0;

    if (bytes_to_send == 0)  // 响应报文为空
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while( 1 )
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);  // 将响应报文的状态行、消息头、空行和响应正文发送给浏览器端

        //正常发送，temp为发送的字节数
        if (temp > 0)
        {
            //更新已发送字节
            bytes_have_send += temp;
            //偏移文件iovec的指针
            newadd = bytes_have_send - m_write_idx;
        }

        if (temp <= -1)
        {
            if(errno == EAGAIN)  // 缓冲区满了
            {
                //第一个iovec头部信息的数据已发送完，发送第二个iovec数据
                if (bytes_have_send >= m_iv[0].iov_len)
                {
                    //不再继续发送头部信息
                    m_iv[0].iov_len = 0;
                    m_iv[1].iov_base = m_file_address + newadd;
                    m_iv[1].iov_len = bytes_to_send;
                }
                //继续发送第一个iovec头部信息的数据
                else
                {
                    m_iv[0].iov_base = m_write_buf + bytes_to_send;
                    m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
                }
                //重新注册写事件
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();  //如果发送失败，但不是缓冲区问题，取消映射
            return false;
        }

        bytes_to_send -= temp;  
        bytes_have_send += temp;  // 更新已发送字节数

        if (bytes_to_send <= 0)  // 数据发送完毕
        {
            unmap();
            if (m_linger)  // 长连接
            {
                init();  // 重新初始化HTTP对象
                modfd(m_epollfd, m_sockfd, EPOLLIN);  // 在epoll树上重置EPOLLONESHOT事件
                return true;
            }
            else
            {
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return false;
            } 
        }
    }
}

// 报文解析
http_conn::HTTP_CODE http_conn::process_read()
{
    // 初始化
    LINE_STATUS line_status = LINE_OK;  // 从状态机状态
    HTTP_CODE ret = NO_REQUEST;  // 解析结果
    char *text = 0;

    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || 
        ((line_status = parse_line()) == LINE_OK))  // TODO
    {
        text = get_line();
        m_start_line = m_checked_idx;  // TODO
        printf("got 1 http line: %s\n", text);

        // 主状态机三种状态转移逻辑
        switch (m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:  // 请求行
            {
                ret = parse_request_line(text);  // 解析请求行
                if (ret == BAD_REQUEST)  // 请求报文语法错误
                {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:  // 请求头
            {
                ret = parse_request_headers(text);  // 解析请求头
                if ( ret == BAD_REQUEST )
                {
                    return BAD_REQUEST;
                }
                else if (ret == GET_REQUEST)  // 解析GET请求后跳转到报文响应函数
                {
                    return do_request();  // 报文响应函数
                }
                break;
            }
            case CHECK_STATE_CONTENT:  // 消息体
            {
                ret = parse_content(text);  // 解析消息体
                if ( ret == GET_REQUEST )  // 解析POST请求后跳转到报文响应函数
                {
                    return do_request();
                }
                line_status = LINE_OPEN;  // 完成报文解析，更新line_status，避免再次进入循环
                break;
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}
