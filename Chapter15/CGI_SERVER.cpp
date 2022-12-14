#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "processPoll.h"

class cgi_conn
{
public:
    cgi_conn() {}
    ~cgi_conn() {}

    void init(int epollfd, int sockfd, const sockaddr_in &client_addr)
    {
        m_epollfd = epollfd;
        m_sockfd = sockfd;
        m_address = client_addr;
        memset(m_buff, '\0', 1024);
        m_read_idx = 0;
        char szAddress[64] = {0};
        inet_ntop(AF_INET, (void *)&m_address.sin_addr, szAddress, 64);
        int port = ntohs(client_addr.sin_port);
        printf("receive client %s:%u connected\n",szAddress,port);
    }

    void process()
    {
        int idx = 0;
        int ret = -1;
        while (true)
        {
            idx = m_read_idx;
            ret = recv(m_sockfd, m_buff + idx, BUFFER_SIZE - 1, 0);
            if (ret < 0)
            {
                if (errno != EAGAIN)
                {
                    removefd(m_epollfd, m_sockfd);
                }
                break;
            }
            else if (ret == 0)
            {
                removefd(m_epollfd, m_sockfd);
                break;
            }
            else
            {
                m_read_idx += ret;
                printf("user content is %s\n", m_buff);
                for (; idx < m_read_idx; ++idx)
                {
                    if (idx >= 1 && m_buff[idx - 1] == '\r' && m_buff[idx] == '\n')
                    {
                        break;
                    }
                }
                if (idx == m_read_idx)
                {
                    continue;
                }
                m_buff[idx - 1] = '\0';

                char *file_name = m_buff;
                if (access(file_name, F_OK) == -1)
                {
                    removefd(m_epollfd, m_sockfd);
                    break;
                }
                ret = fork();
                if (ret == -1)
                {
                    removefd(m_epollfd, m_sockfd);
                    break;
                }
                else if (ret > 0)
                {
                    removefd(m_epollfd,m_sockfd);
                    break;
                }
                else
                {
                    close(STDOUT_FILENO);
                    dup(m_sockfd);
                    execl(m_buff,m_buff,0);
                    exit(0);
                }
            }
        }
    }

private:
    static const int BUFFER_SIZE = 1024;
    static int m_epollfd;
    int m_sockfd;
    sockaddr_in m_address;
    char m_buff[1024];
    int m_read_idx;
};



int cgi_conn::m_epollfd = -1;

int main(int argc,char* argv[])
{
    if(argc <= 2)
    {
        printf("usage: %s ip_address port_number\n",basename(argv[0]));
        return 1;
    }

    const char* ip = argv[1];
    int port = atoi(argv[2]);

    int listenfd = socket(PF_INET,SOCK_STREAM,0);
    assert(listenfd != -1);

    unsigned int opt = 1; // sockfd?????????????????????????????????
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&opt, sizeof(opt));

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address,sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET,ip,&address.sin_addr);
    address.sin_port = htons(port);

    ret = bind(listenfd,(struct sockaddr*)&address,sizeof(address));
    assert(ret != -1);

    ret = listen(listenfd,5);
    assert(ret != -1);

    processPool<cgi_conn>* pool = processPool<cgi_conn>::create(listenfd);
    if(pool)
    {
        pool->run();
        delete pool;
    }
    close(listenfd);
    return 0;
}