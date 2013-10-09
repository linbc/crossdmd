#include <assert.h>
#include <iostream>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <fcntl.h>
#include <netinet/in.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <errno.h>

#include <map>

#define oops(msg) { perror("oops:" msg); }

#define min(x,y) ({ \
    __typeof(x) _x = (x); \
    __typeof(y) _y = (y); \
    (void) (&_x == &_y); \
    _x < _y ? _x : _y; })

#define max(x,y) ({ \
    __typeof(x) _x = (x); \
    __typeof(y) _y = (y); \
    (void) (&_x == &_y); \
    _x > _y ? _x : _y; })

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

namespace g_xxx
{
    int g_n_robot = 1;
    char *g_svr_addr = NULL;
    int g_port = 0;
}

typedef int64_t time_in_ms;

struct robot_t
{
    pthread_t t_id; // 所属线程id
    int idx;

    int sockfd;

    bool closed; // 是否已关闭
    int error_no; // 错误号

    time_in_ms start_conn_time;
    time_in_ms conn_succ_time;
    time_in_ms finish_time;

    time_in_ms error_occur_time;
};

namespace monitoring
{
    #define  MS_PER_SECOND 1000

    struct robot_monitor
    {
        int64_t start_ms;
        int64_t end_ms;

        int n_robot;
    };

    struct robot_chart
    {
        int n_succ;
        int n_fail;

        typedef int error_type;
        typedef int occur_num;

        typedef std::map<error_type, occur_num> error_map_t;
        error_map_t error_map;
    };

    int64_t now()
    {
        struct timeval time;
        gettimeofday(&time, NULL);

        return time.tv_sec * 1000 + time.tv_usec / 1000;
    }

    void chart_init(robot_chart &chart)
    {
        chart.n_succ = 0;
        chart.n_fail = 0;
    }

    void chart_sum_up(robot_chart &chart, const robot_t *robot)
    {
        if(robot->error_no == 0)
        {
            chart.n_succ++;
        }
        else
        {
            chart.n_fail++;
            chart.error_map[robot->error_no]++;
        }
    }

    void print_robot_chart(robot_chart &chart)
    {
        typedef robot_chart::error_map_t error_map_t;

        fprintf(stdout, "robot succ num = %d\n", chart.n_succ);
        fprintf(stdout, "robot fail num = %d\n", chart.n_fail);

        for(error_map_t::iterator itr = chart.error_map.begin(); itr != chart.error_map.end(); itr++)
        {
            int err_type = itr->first;
            int err_occur_num = itr->second;

            fprintf(stdout, "num of error<%d, %s> is %d\n", err_type, strerror(err_type), err_occur_num);
        }
    }

    double ms_to_s(double ms)
    {
        double s = ms / MS_PER_SECOND;
        return s;
    }

    void monitor_start(robot_monitor &monitor)
    {
        monitor.start_ms = now();
        monitor.n_robot = 0;
    }

    void monitor_end(robot_monitor &monitor)
    {
        monitor.end_ms = now();
    }

    void monitor_print(robot_monitor &monitor, const robot_t robots[], int n_robot)
    {
        robot_chart chart;
        chart_init(chart);

        fprintf(stdout, "/----------------------------------------\n");

        int total_conn_time = 0;
        int total_send_recv_time = 0;
        int total_succ_robot_life = 0;

        int n_conn_succ = 0;
        int n_safebox_succ = 0;
        
#define MS_PER_MINUTE 60000

        int min_conn_time = MS_PER_MINUTE;
        int max_conn_time = 0;

        int min_send_recv_time = MS_PER_MINUTE;
        int max_send_recv_time = 0;

        int min_robot_life = MS_PER_MINUTE;
        int max_robot_life = 0;

        for(int i = 0; i < n_robot; i++)
        {
            // fprintf(stdout, "<robot %d>", i);

            const robot_t *robot = &robots[i];
            if(NULL == robot)
            {
                fprintf(stdout, "null");
                continue;
            }

            if(robot->conn_succ_time)
            {
                ++n_conn_succ;

                int conn_time = robot->conn_succ_time - robot->start_conn_time;
                total_conn_time += conn_time;

                if(robot->finish_time)
                {
                    ++n_safebox_succ;

                    int robot_life = robot->finish_time - robot->start_conn_time;
                    int send_recv_time = robot_life - conn_time;

                    total_succ_robot_life += robot_life;
                    total_send_recv_time += send_recv_time;
                    
                    min_send_recv_time = min(send_recv_time, min_send_recv_time);
                    max_send_recv_time = max(send_recv_time, max_send_recv_time);

                    min_robot_life = min(robot_life, min_robot_life);
                    max_robot_life = max(robot_life, max_robot_life);
                }

                min_conn_time = min(conn_time, min_conn_time);
                max_conn_time = max(conn_time, max_conn_time);
            }
            
            chart_sum_up(chart, robot);

            // fprintf(stdout, "robot fd = %d, ", conn->fd);
            if(robot->error_no)
            {
                int err_time_diff = robot->error_occur_time - robot->start_conn_time;
                fprintf(stdout, "robot[idx=%d,fd=%d] failed after launched <%f> s, errormsg: <%d>%s\n", 
                    robot->idx, robot->sockfd, ms_to_s(err_time_diff), robot->error_no, strerror(robot->error_no));
            }
        }

        int total_cost_time = (int)(monitor.end_ms - monitor.start_ms);

        double avg_conn_ms = (n_conn_succ == 0 ? 0 : (double)total_conn_time / n_conn_succ);
        double avg_send_recv_ms = ((n_safebox_succ == 0) ? 0 : (double)total_send_recv_time / n_safebox_succ);
        double avg_robot_life = ((n_safebox_succ == 0) ? 0 : (double)total_succ_robot_life / n_safebox_succ);

        double conn_per_sec = ((n_conn_succ == 0) ? 0 : (double)n_conn_succ / ms_to_s(max_conn_time));
        double robots_per_sec = ((n_safebox_succ == 0) ? 0 :(double)n_safebox_succ / ms_to_s(max_robot_life));

        fprintf(stdout, "total launched robot number = %d\n", n_robot);
        print_robot_chart(chart);

        // fprintf(stdout, "conn succ num = %d\n", n_conn_succ);
        // fprintf(stdout, "safebox succ num = %d\n", n_safebox_succ);

        fprintf(stdout, "\n");

        fprintf(stdout, "total cost = %f s\n", ms_to_s(total_cost_time));

        fprintf(stdout, "\n");

        fprintf(stdout, "min conn time = %f s\n", ms_to_s(min_conn_time));
        fprintf(stdout, "max conn time = %f s\n", ms_to_s(max_conn_time));

        fprintf(stdout, "min send recv time = %f s\n", ms_to_s(min_send_recv_time));
        fprintf(stdout, "max send recv time = %f s\n", ms_to_s(max_send_recv_time));

        fprintf(stdout, "min robot life = %f s\n", ms_to_s(min_robot_life));
        fprintf(stdout, "max robot life = %f s\n", ms_to_s(max_robot_life));

        fprintf(stdout, "\n");

        fprintf(stdout, "avg conn time = %f s\n", ms_to_s(avg_conn_ms));
        fprintf(stdout, "avg send_recv time = %f s\n", ms_to_s(avg_send_recv_ms));
        fprintf(stdout, "avg robot life = %f s\n", ms_to_s(avg_robot_life));

        fprintf(stdout, "\n");

        fprintf(stdout, "conns per second = %f\n", conn_per_sec);
        fprintf(stdout, "robots per second = %f\n", robots_per_sec);//(double)MS_PER_SECOND / avg_robot_life);

        fprintf(stdout, "----------------------------------------/\n");
    }
}

using namespace g_xxx;
using namespace monitoring;

void print_host_ent(hostent *host_ent, const char *host_name)
{
    char **pptr;

    char str[32];

    /* 将主机的规范名打出来 */  
    fprintf(stdout, "resolve url<%s> success, the official hostname = <%s>, ", host_name, host_ent->h_name);

    /* 主机可能有多个别名，将所有别名分别打出来 */
    for (pptr = host_ent->h_aliases; *pptr != NULL; pptr++)   
    {
        fprintf(stdout, "alias:%s\n", *pptr);
    }

    /* 根据地址类型，将地址打出来 */ 
    switch(host_ent->h_addrtype)   
    {   
    case AF_INET:   
    case AF_INET6:   
        pptr = host_ent->h_addr_list;   

        /* 将得到的所有地址都打出来。其中调用了inet_ntop()函数 */  
        for(;*pptr!=NULL;pptr++) 
        {
            printf("ip = <%s>\n", inet_ntop(host_ent->h_addrtype, *pptr, str, sizeof(str)));   
        }

        break;   

    default:   
        printf("unknown address type\n");   
        break;   
    }
}

unsigned long name_resolve(const char *host_name)  
{  
    struct in_addr addr;  

    if((addr.s_addr=inet_addr(host_name)) == (unsigned)-1) 
    {  
        struct hostent *host_ent = gethostbyname(host_name);  
        if(host_ent==NULL)
        {
            fprintf(stdout, "name_resolve fail\n");
            return(-1);
        }

        memcpy((char *)&addr.s_addr, host_ent->h_addr, host_ent->h_length);  

        print_host_ent(host_ent, host_name);
    }

    return addr.s_addr;
}

int robot_connect(const char svr_address[], int port)
{
    static unsigned long cast_svr_addr = name_resolve(svr_address);

    int client_socket = socket(AF_INET, SOCK_STREAM, 0);  
    if(client_socket < 0)
    {
        oops("socket initiating error...");
        return -1;
    }

    struct sockaddr_in svr_addr;  
    memset(&svr_addr, 0, sizeof(sockaddr));

    svr_addr.sin_family = AF_INET;
    svr_addr.sin_addr.s_addr = cast_svr_addr;  
    svr_addr.sin_port =  htons(port); 

    int connect_result = connect(client_socket, (struct sockaddr*)&svr_addr, sizeof(svr_addr));  
    if(connect_result < 0)
    {
        oops("connect error...");
        return -1;
    }

    // setnonblocking(client_socket);
    return client_socket;
}

void set_socket_send_timeout(int sockfd, long tv_sec, long tv_usec)
{
    struct timeval timeout;             //超时时间
    timeout.tv_sec = tv_sec;
    timeout.tv_usec = tv_usec;

    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,sizeof(struct timeval)); 
}

void set_socket_recv_timeout(int sockfd, long tv_sec, long tv_usec)
{
    struct timeval timeout;             //超时时间
    timeout.tv_sec = tv_sec;
    timeout.tv_usec = tv_usec;

    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,sizeof(struct timeval)); 
}

bool robot_send(robot_t *robot, const char buf[], int n)
{
    set_socket_send_timeout(robot->sockfd, 5, 0);

    ssize_t write_length = send(robot->sockfd, buf, n, 0);
    if(write_length < n)
    {
        oops("error: write ...");
        // close(client_socket);

        return false;
    }

    return true;
}

bool robot_recv(robot_t *robot, char buf[], int buf_len, ssize_t &n_recv)
{
    set_socket_recv_timeout(robot->sockfd, 5, 0);

    n_recv = recv(robot->sockfd, buf, buf_len, MSG_WAITALL);  
    if(n_recv == -1)
    {
        fprintf(stdout, "robot<idx=%d, fd=%d> recv msg fail! error code is <%d>, error msg is <'%s'>\n", robot->idx, robot->sockfd, errno, strerror(errno));

        // oops("error: read data from socket...");
        return false;
    }

    buf[n_recv] = '\0';

    return true;
}

void robot_end(robot_t *robot)
{
    if(0 == robot->error_no)
    {
        robot->finish_time = now();    
    }

    close(robot->sockfd);
}

void robot_err_cache(robot_t *robot)
{
    robot->error_no = errno;
    robot->error_occur_time = now();
}

void robot_start(robot_t *robot, const char svr_addr[], int port)
{
    robot->start_conn_time = now();

    int robot_socket = robot_connect(svr_addr, port);
    if(robot_socket < 0)
    {
        fprintf(stderr, "robot<idx=%d> could not connect to server<%s:%d>\n", robot->idx, svr_addr, port);
        robot_err_cache(robot);
        return;
    }

    robot->sockfd = robot_socket;
    robot->conn_succ_time = now();

    fprintf(stderr, "robot<idx=%d, fd=%d> connect to server<%s:%d>success\n", robot->idx, robot->sockfd, svr_addr, port);

    static const char buf[] = "<?xml version=\"1.0\"?>*****************";
    int len = strlen(buf) + 1;

    bool succ = robot_send(robot, buf, len);
    if(false == succ)
    {
        robot_err_cache(robot);
        return;
    }

    char recv_buf[256] = {0};
    ssize_t n_recv = 0;

    succ = robot_recv(robot, recv_buf, sizeof(recv_buf), n_recv);
    if(false == succ)
    {
        robot_err_cache(robot);
        return;
    }

    static const char* expected_data = 
        "<?xml version=\"1.0\"?>"
        "<!DOCTYPE cross-domain-policy SYSTEM \"http://www.macromedia.com/xml/dtds/cross-domain-policy.dtd\">"
        "<cross-domain-policy>"
        "	<allow-access-from domain=\"*\" to-ports=\"*\" />"
        "</cross-domain-policy>";

    ssize_t expected_len = strlen(expected_data) + 1;
    if(strncmp(recv_buf, expected_data, strlen(expected_data)) || n_recv != expected_len)
    {
        fprintf(stderr, "recv data err, unexpected data:%s, expecting len is %zd, recv len is %zd, robot<idx=%d, fd=%d>\n", 
            recv_buf, expected_len, n_recv, robot->idx, robot->sockfd);
        fprintf(stderr, "              xpecting data is:%s,\n", expected_data);
    }

    // fprintf(stdout, "robot <%d> life is %d ms\n", i, (int)(end_ms - start_ms));

    robot_end(robot);
}

void* robot_thread_cb(void *un_used)
{
    struct robot_t *robot = (robot_t*)un_used;
	robot_start(robot, g_svr_addr, g_port);

	return NULL;
}

void new_thread_robot(robot_t *robot)
{
	pthread_t tid = 0;
	pthread_create(&tid, NULL, robot_thread_cb, robot); 

    robot->t_id = tid;
	// pthread_join(tid, NULL);
}

void robots_power_on()
{
    robot_monitor monitor;
    monitor_start(monitor);

    struct robot_t *robots = new robot_t[g_n_robot];
    for(int i = 0; i < g_n_robot; i++)
    {
        robot_t *robot = &robots[i];
        memset(robot, 0, sizeof(robot_t));

        robot->idx = i;
        new_thread_robot(robot);
    }

    for(int i = 0; i < g_n_robot; i++)
    {
        robot_t *robot = &robots[i];
        pthread_join(robot->t_id, NULL);
    }

    monitor_end(monitor);
    monitor_print(monitor, robots, g_n_robot);
}

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "your command is invalid, check if it is:[robot server_url port robot_num]\n");

        fprintf(stderr, "for example: robot s0.9.game2.com.cn 843 10000\n");
        fprintf(stderr, "for example: robot 127.0.0.1 10023 999\n");

        return 1;
    }

    g_svr_addr = argv[1];
    if((unsigned long)-1 == name_resolve(g_svr_addr))
    {
        fprintf(stderr, "url:<%s> is invalid\a\n",argv[1]);
        return 1;
    }
    if((g_port = atoi(argv[2])) < 0 )
    {
        fprintf(stderr, "url:<%s> is invalid\a\n",argv[2]);
        return 1;
    }
    if( (g_n_robot = atoi(argv[3])) < 0 )
    {
        fprintf(stderr, "robot number:<%s> is invalid\a\n",argv[3]);
        return 1;
    }

    fprintf(stdout, "plan to launch <%d> robot\n", g_n_robot);

	robots_power_on();

	fprintf(stdout, "main is ending, robot num is %d\n", g_n_robot);

	return 0;
}
