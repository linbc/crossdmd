///<-----------------------------------------------------------
//# 
//#  @description: ��ȫɳ�������
//#  @create date: 2013-10-9 
//# 
//<------------------------------------------------------------/

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

namespace g_xxx
{
    const char g_safebox_buf[] = 
        "<?xml version=\"1.0\"?>"
        "<!DOCTYPE cross-domain-policy SYSTEM \"http://www.macromedia.com/xml/dtds/cross-domain-policy.dtd\">"
        "<cross-domain-policy>"
        "	<allow-access-from domain=\"*\" to-ports=\"*\" />"
        "</cross-domain-policy>";

    const int g_safebox_data_len = strlen(g_safebox_buf) + 1;

}

using namespace g_xxx;

namespace log
{
#define LOG_TRACE(xxx, args...) {fprintf(stdout, xxx, ##args);}
#define LOG_ERR(xxx, args...) {fprintf(stderr, xxx, ##args);}
}

using namespace log;

bool setnonblocking(int sock)
{
    int opts = fcntl(sock,F_GETFL);
    if(opts < 0)
    {
        // perror("fcntl(sock,GETFL)");
        return false;
    }

    opts = opts | O_NONBLOCK;
    if(fcntl(sock,F_SETFL,opts) < 0)
    {
        // perror("fcntl(sock,SETFL,opts)");
        return false;
    }

    return true;
}

void set_addr_reused(int socket)
{
    int opt = 1; 
    setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); 
}

void handle_send(int sockfd)
{
    int n = send(sockfd, g_safebox_buf, g_safebox_data_len, 0);
    if (n < 0)
    {
        if(errno == EAGAIN || errno == EINTR || errno == EWOULDBLOCK)
        {
            // LOG_TRACE("robot send msg fail! error code is <%d>, error msg is <'%s'>\n", errno, strerror(errno));
        }
        else
        {// ˵������Ӧ�ùر�����
            // LOG_ERR("send msg fail! error code is <%d>, error msg is <'%s'>\n", errno, strerror(errno));
            close(sockfd);
        }

        return;
    }
    
    if(n == 0)
    {
        // LOG_TRACE("svr side detected the socket<%d> closed\n", sockfd);
    }

    close(sockfd);
    return;
}

bool try_send_safebox_data(int sockfd)
{
    int n_send = send(sockfd, g_safebox_buf, g_safebox_data_len, 0);
    if(n_send < 0)
    {
        // LOG_ERR("try to send msg to robot <fd=%d> not in epoll fail, ! error code is <%d>, error msg is <'%s'>\n", sockfd, errno, strerror(errno));
        return false;
    }

    // LOG_ERR("try to send msg to robot <fd=%d> not in epoll success\n", sockfd);
    close(sockfd);
    return true;
}

void handle_msg(int sockfd, epoll_event &in_ev, int epfd)
{
    static char buf[MAX_BUF + 1];

    int n = 0;
    ssize_t nread = 0;

    nread = recv(sockfd, buf + n, MAX_BUF, 0);
    if(nread < 0)
    {
        if(errno == EAGAIN || errno == EINTR || errno == EWOULDBLOCK)
        {
            // LOG_TRACE("robot <fd=%d> eagain recv msg fail! error code is <%d>, error msg is <'%s'>\n", fd, errno, strerror(errno));
        }
        else
        {
            // LOG_TRACE("sockfd is <%d>, error type is <%d>, error msg is <'%s'>\n", sockfd, errno, strerror(errno));
            close(sockfd);
        }

        return;
    }

    if(nread == 0)
    {// peer close the conn
        close(sockfd);
        return;
    }
     
    n += nread;
    buf[n] = '\0';

    // printf("socket:%d recv msg:'%s', total %d bytes\n", sockfd, buf, n);

    // �ȳ���ֱ��send��ʧ���ټ���epoll
    if(false == try_send_safebox_data(sockfd))
    {// ֱ��sendʧ����
        struct epoll_event ev;
        ev.data.fd = sockfd;
        ev.events = in_ev.events | EPOLLOUT;

        if (epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev) == -1) 
        {
            // perror("epoll_ctl: mod");
        }
    }
    
}

void handle_accept(int listenfd, epoll_event &ev, int epfd)
{
    int connfd;

    socklen_t clilen = sizeof(struct sockaddr);
    struct sockaddr_in clientaddr;

    while((connfd = accept(listenfd, (struct sockaddr*)&clientaddr, &clilen)) > 0)
    {
        if(false == setnonblocking(connfd))
        {
            continue;
        }

        // LOG_TRACE("accept connection from: <%s:%d>, assigned socket is:<%d>\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port), connfd);
        ev.data.fd = connfd;
        ev.events = EPOLLIN;// | EPOLLET;

        if (epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev) == -1) {
            // perror("epoll_ctl: add");
            break;
        }
    }

    if (connfd == -1) {
        if (errno != EAGAIN && errno != ECONNABORTED && errno != EPROTO && errno != EINTR)
        {
            return;
        }
    }
}

// ��ȫɳ����Է�����
int main(int argc, char* argv[])
{
    int i, sockfd, nfds;

    int port = 843; // �˿�
    if (2 != argc && 1!= argc)
    {
        LOG_ERR("your command is invalid, check if it is:\n");
        LOG_ERR("   sandboxsvr [port]\n");

        LOG_ERR("\n");

        LOG_ERR("   for example: sandboxsvr 843\n");

        return 1;
    }
    
    if(2 == argc)
    {// ˵����ָ���˿�
        if( (port = atoi(argv[1])) <= 0 )
        {
            LOG_ERR("<%s> is not a valid port\n",argv[0]);
            return 1;
        }
    }

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    setnonblocking(listenfd);//��socket����Ϊ��������ʽ
    set_addr_reused(listenfd);//set socket can be reused

    //����epoll_event�ṹ��ı���,ev����ע���¼�,�������ڻش�Ҫ������¼�
    struct epoll_event ev,events[MAX_EVENT];

    ev.data.fd = listenfd;//������Ҫ������¼���ص��ļ�������
    ev.events = EPOLLIN|EPOLLET;//����Ҫ������¼�����

    //�������ڴ���accept��epollר�õ��ļ�������
    int epfd = epoll_create(256);
    epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);//ע��epoll�¼�

    struct sockaddr_in serveraddr;

    bzero(&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = INADDR_ANY;
    serveraddr.sin_port = htons(port);

    if(bind(listenfd, (sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
    {
        LOG_ERR("bind <listenfd = %d> at %s:%d fail! <error=%d>'%s'\n", listenfd, inet_ntoa(serveraddr.sin_addr), ntohs(serveraddr.sin_port), errno, strerror(errno));
        return 1;
    }

    if(listen(listenfd, LISTENQ) < 0)
    {
        LOG_ERR("socketfd = %d listen at %s:%d fail! <error=%d>'%s'\n", listenfd, inet_ntoa(serveraddr.sin_addr), ntohs(serveraddr.sin_port), errno, strerror(errno));
        return 1;
    }

    LOG_TRACE("start listenning at %s:%d, socket is :%d\n", inet_ntoa(serveraddr.sin_addr), ntohs(serveraddr.sin_port), listenfd);

    while(true)
    {
        //�ȴ�epoll�¼��ķ���
        nfds = epoll_wait(epfd, events, MAX_EVENT, -1);

        //�����������������¼�
        for(i = 0; i < nfds; ++i)
        {
            if(events[i].data.fd == listenfd)//����¼�⵽һ��SOCKET�û����ӵ��˰󶨵�SOCKET�˿ڣ������µ����ӡ�
            {
                handle_accept(listenfd, ev, epfd);
                continue;
            }
            if(events[i].events & EPOLLIN)//������Ѿ����ӵ��û��������յ����ݣ���ô���ж��롣
            {
                if ( (sockfd = events[i].data.fd) < 0)
                {
                    continue;
                }

                handle_msg(sockfd, events[i], epfd);
            }
            if(events[i].events & EPOLLOUT) // ��������ݷ���
            {
                handle_send(events[i].data.fd);
            }
        }
    }

    close(listenfd);
    return 0;
}
