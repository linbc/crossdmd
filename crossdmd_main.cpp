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

void setnonblocking(int sock)
{
    int opts = fcntl(sock,F_GETFL);
    if(opts < 0)
    {
        perror("fcntl(sock,GETFL)");
        return;
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
    int n = send(sockfd, g_safebox_buf, g_safebox_data_len, 0);
    if (n < 0)
    {
        if(errno == EAGAIN || errno == EINTR || errno == EWOULDBLOCK)
        {
            // fprintf(stdout, "robot send msg fail! error code is <%d>, error msg is <'%s'>\n", errno, strerror(errno));
        }
        else
        {// ˵������Ӧ�ùر�����
            fprintf(stderr, "send msg fail! error code is <%d>, error msg is <'%s'>\n", errno, strerror(errno));
            close(sockfd);
        }

        return;
    }
    
    if(n == 0)
    {
        // fprintf(stdout, "svr side detected the socket<%d> closed\n", sockfd);
    }

    close(sockfd);
    return;
}

bool try_send_safebox_data(int sockfd)
{
    int n_send = send(sockfd, g_safebox_buf, g_safebox_data_len, 0);
    if(n_send < 0)
    {
        // fprintf(stderr, "try to send msg to robot <fd=%d> not in epoll fail, ! error code is <%d>, error msg is <'%s'>\n", sockfd, errno, strerror(errno));
        return false;
    }

    // fprintf(stderr, "try to send msg to robot <fd=%d> not in epoll success\n", sockfd);
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
            // fprintf(stdout, "robot <fd=%d> eagain recv msg fail! error code is <%d>, error msg is <'%s'>\n", fd, errno, strerror(errno));
        }
        else
        {
            fprintf(stdout, "sockfd is <%d>, error type is <%d>, error msg is <'%s'>\n", sockfd, errno, strerror(errno));
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
            perror("epoll_ctl: mod");
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
        setnonblocking(connfd);

        // fprintf(stdout, "accept connection from: <%s:%d>, assigned socket is:<%d>\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port), connfd);
        ev.data.fd = connfd;
        ev.events = EPOLLIN;// | EPOLLET;

        if (epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev) == -1) {
            perror("epoll_ctl: add");
            break;
        }
    }

    if (connfd == -1) {
        if (errno != EAGAIN && errno != ECONNABORTED && errno != EPROTO && errno != EINTR)
        {
            printf("bad accept\n");
            return;
        }
    }
}

// ��ȫɳ����Է�����
int main(int argc, char* argv[])
{
    int i, sockfd, nfds;

    int port = 0; // �˿�
    if (2 != argc)
    {
        fprintf(stderr, "your command is invalid, check if it is:[sandboxsvr port]\n");
        fprintf(stderr, "for example: sandboxsvr 843\n");

        return 1;
    }
    
    if( (port = atoi(argv[1])) < 0 )
    {
        fprintf(stderr,"port<%s> err\n",argv[0]);
        return 1;
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
        perror("bind failed");
        return 1;
    }

    if(listen(listenfd, LISTENQ) < 0)
    {
        perror("listen failed");
        return 1;
    }

    fprintf(stdout, "start listenning at %s:%d, socket is :%d\n", inet_ntoa(serveraddr.sin_addr), ntohs(serveraddr.sin_port), listenfd);

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
