#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <libgen.h>

#include "lock/locker.h"
#include "threadpool/threadpool.h"
#include "http/httpconn.h"

int main(int argc, char *argv[])
{
    if (argc <= 1)
    {   
        printf("usage: %s ip_address port_number\n", basename(argv[0]));  // basename(): 获取路径中的文件名
        return 1;
    }
    const char *ip = argv[1];
    int port = atoi(argv[2]);

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
    
}