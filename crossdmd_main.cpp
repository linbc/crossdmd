#include <iostream>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <map>
#include <stdlib.h>

using namespace std;

#define LISTENQ 102400

#define MAX_BUF 1024
#define MAX_EVENT 10240

struct fd_entry
{
    int sended;
    int recved;
};

typedef int fd_t;
typedef int nsended;
std::map<fd_t, nsended> fd_map;

void setnonblocking(int sock)
{
    int opts = fcntl(sock,F_GETFL);
    if(opts < 0)
    {
        perror("fcntl(sock,GETFL)");
    }

    opts = opts | O_NONBLOCK;
    if(fcntl(sock,F_SETFL,opts) < 0)
    {
        perror("fcntl(sock,SETFL,opts)");
    }
}

void set_addr_reused(int socket)
{
    int opt = 1; 
    setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); 
}

void handle_send(int sockfd)
{
    static const char buf[] = 
        "<?xml version=\"1.0\"?>"
        "<!DOCTYPE cross-domain-policy SYSTEM \"http://www.macromedia.com/xml/dtds/cross-domain-policy.dtd\">"
        "<cross-domain-policy>"
        "	<allow-access-from domain=\"*\" to-ports=\"*\" />"
        "</cross-domain-policy>";

    int size = strlen(buf) + 1;

    int nsended = fd_map[sockfd];
    int remain = size - nsended;

    if(nsended != 0)
    {
        fprintf(stdout,"nsended is :%d\n", nsended);
    }

    int n = send(sockfd, buf + size - remain, remain, 0);
    if (n < 0)
    {
        if(errno == EAGAIN || errno == EINTR || errno == EWOULDBLOCK)
        {
            fprintf(stdout, "robot send msg fail! error code is <%d>, error msg is <'%s'>\n", errno, strerror(errno));
            return;
        }
        else
        {
            fprintf(stderr, "send msg fail! error code is <%d>, error msg is <'%s'>\n", errno, strerror(errno));

            fd_map[sockfd] = 0;
            close(sockfd);

            return;
        }
    }
    else if(n == 0)
    {
        fprintf(stdout, "svr side detected the socket<%d> closed\n", sockfd);

        close(sockfd);
        fd_map[sockfd] = 0;

        return;
    }

    remain -= n;

    // printf("fd = %d,remain = %d\n", sockfd, remain);
    if (remain > 0) 
    {
        fd_map[sockfd] = size - remain;
    }
    else if(remain == 0)
    {// 发送完毕
        fd_map[sockfd] = 0;
        close(sockfd);
    }
    else
    {

    }
}


void handle_msg(int sockfd, epoll_event &in_ev, int epfd)
{
    char buf[MAX_BUF + 1];

    int n = 0;
    ssize_t nread = 0;


    nread = recv(sockfd, buf + n, MAX_BUF, 0);
    if(nread < 0)
    {
        if(errno == EAGAIN || errno == EINTR || errno == EWOULDBLOCK)
        {
            // fprintf(stdout, "robot <fd=%d> eagain recv msg fail! error code is <%d>, error msg is <'%s'>\n", fd, errno, strerror(errno));
            return;
        }
        else
        {
            perror("read error");

            fprintf(stdout, "sockfd is <%d>, error type is <%d>, error msg is <'%s'>\n", sockfd, errno, strerror(errno));

            close(sockfd);

            return;
        }
    }    
    else if(nread == 0)
    {// peer close the conn
        close(sockfd);
        return;
    }
    else
    {
        n += nread;
    }
    
    buf[n] = '\0';

    // printf("socket:%d recv msg:'%s', total %d bytes\n", sockfd, buf, n);

    struct epoll_event ev;
    ev.data.fd = sockfd;
    ev.events = in_ev.events | EPOLLOUT;
    if (epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev) == -1) 
	{
        perror("epoll_ctl: mod");
    }
}

int64_t now()
{
    struct timeval time;
    gettimeofday(&time, NULL);

    return time.tv_sec * 1000 + time.tv_usec / 1000;
}

struct conn_watcher
{
    int conn;// conns num between [now_ms - 1000ms, now_ms]
    int64_t now_ms;
};

struct conn_chart
{
    conn_watcher watchers[1024];
    size_t len;
};

void chart_append(conn_chart* chart, int conn, int64_t now_ms)
{
    if(chart->len >= sizeof(chart->watchers) / sizeof(conn_watcher))
    {
        std::cout << "conn_chart append() failed: too much elements" << std::endl;
        return;
    }

    if(0 == chart->len)
    {
        chart->watchers[chart->len].conn = conn;
    }
    else
    {
        for(size_t i = 0; i < chart->len; i++)
        {
            conn -= chart->watchers[i].conn;
        }

        chart->watchers[chart->len].conn = conn;
    }

    chart->watchers[chart->len++].now_ms = now_ms;
}

void print(conn_chart* chart)
{
    // fprintf(stdout,"len:%d\n", chart->len);

    for(size_t i = 0; i < chart->len; i++)
    {
        fprintf(stdout,"time:%ld conns:%d\n", chart->watchers[i].now_ms, chart->watchers[i].conn);
    }
}

struct conn_monitor
{
    int64_t start;
    int64_t pre_ms;

    int conn_num;

    conn_chart chart;
};

void monitor_init(conn_monitor *monitor)
{
    monitor->start = 0;
    monitor->pre_ms = 0;
    monitor->conn_num = 0;

    monitor->chart.len = 0;
}

void monitor_on_accept(conn_monitor *monitor)
{
    if(monitor->start == 0)
    {
        monitor->start = now();
        monitor->pre_ms = monitor->start;
    }
}

void monitor_on_send(conn_monitor *monitor)
{
    monitor->conn_num++;

    if(monitor->conn_num % 100 == 0)
    {
        int64_t end = now();
        fprintf(stdout, "cost %ld ms when reached %d connections, conn per second is %lf\n", end - monitor->start, monitor->conn_num, (double)monitor->conn_num / ((double)(end - monitor->start) / 1000));
    }

    int64_t now_ms = now();
    if((now_ms - monitor->pre_ms) > 1000)
    {
        chart_append(&monitor->chart, monitor->conn_num, now_ms);
        monitor->pre_ms = now_ms;
    }

    if(monitor->conn_num % 10000 == 0)
    {
        print(&monitor->chart);
    }
}


void handle_accept(int listenfd, epoll_event &ev, int epfd, conn_monitor *monitor)
{
    int connfd;

    socklen_t clilen = sizeof(struct sockaddr);
    struct sockaddr_in clientaddr;

    while((connfd = accept(listenfd, (struct sockaddr*)&clientaddr, &clilen)) > 0)
    {
        setnonblocking(connfd);

        // fprintf(stdout, "accept connection from: <%s:%d>, assigned socket is:<%d>\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port), connfd);
        ev.data.fd = connfd;
        ev.events = EPOLLIN;// | EPOLLET;

        if (epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev) == -1) {
            perror("epoll_ctl: add");
            break;
        }

        monitor_on_accept(monitor);
    }

    if (connfd == -1) {
        if (errno != EAGAIN && errno != ECONNABORTED && errno != EPROTO && errno != EINTR)
        {
            printf("bad accept\n");
            return;
        }
    }
}

int main(int argc, char* argv[])
{
    int i, sockfd, nfds;

    int port = 0;
    if (2 == argc)
    {
        if( (port = atoi(argv[1])) < 0 )
        {
            fprintf(stderr,"Usage:%s port\a\n",argv[0]);
            return 1;
        }
    }
    else
    {
        fprintf(stderr,"Usage:%s port\a\n",argv[0]);
        return 1;
    }

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    setnonblocking(listenfd);//把socket设置为非阻塞方式
    set_addr_reused(listenfd);//set socket can be reused

    //声明epoll_event结构体的变量,ev用于注册事件,数组用于回传要处理的事件
    struct epoll_event ev,events[MAX_EVENT];

    ev.data.fd = listenfd;//设置与要处理的事件相关的文件描述符
    ev.events = EPOLLIN|EPOLLET;//设置要处理的事件类型

    //生成用于处理accept的epoll专用的文件描述符
    int epfd = epoll_create(256);
    epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);//注册epoll事件

    struct sockaddr_in serveraddr;

    bzero(&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;

    /*
    char local_addr[] ="127.0.0.1";
    inet_aton(local_addr, &(serveraddr.sin_addr));
    */

    serveraddr.sin_addr.s_addr = INADDR_ANY;
    serveraddr.sin_port = htons(port);

    if(bind(listenfd, (sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
    {
        perror("bind failed");
        return 1;
    }

    if(listen(listenfd, LISTENQ) < 0)
    {
        perror("listen failed");
        return 1;
    }

    fprintf(stdout, "start listenning at %s:%d, socket is :%d\n", inet_ntoa(serveraddr.sin_addr), ntohs(serveraddr.sin_port), listenfd);

    conn_monitor monitor;
    monitor_init(&monitor);

    while(true)
    {
        //等待epoll事件的发生
        nfds = epoll_wait(epfd, events, MAX_EVENT, -1);

        //处理所发生的所有事件
        for(i = 0; i < nfds; ++i)
        {
            // fprintf(stdout, "nfds = %d\n", nfds);

            if(events[i].data.fd == listenfd)//如果新监测到一个SOCKET用户连接到了绑定的SOCKET端口，建立新的连接。
            {
                handle_accept(listenfd, ev, epfd, &monitor);
                continue;
            }
            if(events[i].events & EPOLLIN)//如果是已经连接的用户，并且收到数据，那么进行读入。
            {
                // cout << "handle EPOLLIN" << endl;
                if ( (sockfd = events[i].data.fd) < 0)
                {
                    continue;
                }

                handle_msg(sockfd, events[i], epfd);
            }
            if(events[i].events & EPOLLOUT) // 如果有数据发送
            {
                handle_send(events[i].data.fd);

                monitor_on_send(&monitor);
            }
        }
    }

    close(listenfd);
    return 0;
}
