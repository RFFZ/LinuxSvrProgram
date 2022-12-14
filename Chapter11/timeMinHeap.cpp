#ifndef MIN_HEAP_
#define MIN_HEAP_

#include <iostream>
#include <netinet/in.h>
#include <time.h>
#include <stdio.h>
#include <libgen.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

using std::exception;

#define BUFFER_SIZE 64

class heap_timer;
struct client_data
{
    sockaddr_in address;
    int sockfd;
    char buff[BUFFER_SIZE];
    heap_timer *timer;
};

class heap_timer
{
public:
    heap_timer(int delay)
    {
        expire = time(NULL) + delay;
        printf("new heap_timer delay %d expire %ld\n",delay,expire);
    }

public:
    time_t expire;
    void (*cb_func)(client_data *);
    client_data *user_data;
};

class timer_heap
{
public:
    timer_heap(int cap) : capacity(cap), cur_size(0)
    {
        array = new heap_timer*[capacity];
        if(!array)
        {
            throw std::exception();
        }
        for(int i = 0; i<capacity;++i)
        {
            array[i] = NULL;
        }
    }

    timer_heap(heap_timer** init_array,int size,int capacity) : capacity(capacity), cur_size(size)
    {
        if(capacity < size)
        {
            throw std::exception();
        }
        array = new heap_timer*[capacity];
        if(!array)
        {
            throw std::exception();
        }
        for(int i = 0; i< capacity;++i)
        {
            array[i] = NULL;
        }
        if(size == 0)
        {
            for(int i = 0; i< size;++i)
            {
                array[i] = init_array[i];
            }
            for(int i = (cur_size)/2;i >= 0;++i)
            {
                percolate_down(i);
            }
        }
    }

    ~timer_heap()
    {
        for(int i = 0; i<cur_size;++i)
        {
            delete array[i];
        }
        delete []array;
    }

    public:
    void add_timer(heap_timer* timer)
    {
        if(!timer)
        {
            printf("add timer failed for it's null\n");
            return;
        }
        if(cur_size >= capacity)
        {
            resize();
        }

        int hole = cur_size++;
        int parent = 0;
        for(; hole >0; hole = parent)
        {
            parent = (hole -1)/2;
            if(array[parent]->expire <= timer->expire)
            {
                break;
            }
            array[hole] = array[parent];
        }
        array[hole] = timer;

        if(array[0])
        {
            time_t cur = time(NULL);
            int alarmNum = array[0]->expire - cur;
            alarm(alarmNum);
            printf("add timer alarm %d\n",alarmNum);
        }
    }

    void del_timer(heap_timer* timer)
    {
        if(!timer)
        {
            printf("del timer failed for it's null\n");
            return;
        }
        timer->cb_func = NULL;
    }

    heap_timer* top() const
    {
        if(empty())
        {
            return NULL;
        }
        return array[0];
    }

    void pop_timer()
    {
        if(empty())
        {
            printf("pop failed for it's empty\n");
            return;
        }
        if(array[0])
        {
            delete array[0];
            array[0] = array[--cur_size];
            percolate_down(0);
        }
    }

    void tick()
    {
        heap_timer* tmp = array[0];
        time_t cur = time(NULL);
        while(!empty())
        {
            if(!tmp)
            {
                break;
            }
            if (tmp->expire > cur)
            {
                int alarmNum = tmp->expire - cur;
                printf("add timer alarm %d\n", alarmNum);
                alarm(alarmNum);
                break;
            }
            if(array[0]->cb_func)
            {
                array[0]->cb_func(array[0]->user_data);
            }
            pop_timer();
            tmp = array[0];
        }
    }

    bool empty()const{return cur_size == 0;}

    private:
    void percolate_down(int hole)
    {
        heap_timer* temp = array[hole];
        int child = 0;
        for(;((hole*2+1) <= (cur_size -1)) ;hole = child)
        {
            child = hole*2+1;
            if((child < (cur_size-1)) && (array[child+1]->expire < array[child]->expire))
            {
                ++child;
            }
            if(array[child]->expire < temp->expire)
            {
                array[hole] = array[child];
            }
            else
            {
                break;
            }
        }
        array[hole] = temp;
    }


    void resize() 
    {
        heap_timer** temp = new heap_timer*[2*capacity];
        for(int i = 0; i<2*capacity;++i)
        {
            temp[i] = NULL;
        }
        if(!temp)
        {
            throw std::exception();
        }
        capacity = 2*capacity;
        for(int i = 0;i<cur_size;i++)
        {
            temp[i] = array[i];
        }
        delete []array;
        array = temp;
    }

private:
    heap_timer **array;
    int capacity;
    int cur_size;
};
static timer_heap* timeHeap= new timer_heap(100);
static int pipefd[2];
static int epollfd;

#define BUFFER_SIZE 64
#define MAX_EVENT_NUMBER 1024
#define TIMESLOT 5
#define FD_LIMIT 65535

int setnoblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epollfd, int fd)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnoblocking(fd);
}

void sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

void addsig(int sig)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void cb_func(client_data *user_data)
{
    assert(user_data);
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    close(user_data->sockfd);
    printf("close fd %d\n", user_data->sockfd);
}

void timer_handler()
{
    timeHeap->tick();
    
}


int main(int argc, char *argv[])
{
    if (argc <= 2)
    {
        printf("usage:%s ip_address port_number backlog\n", basename(argv[0]));
        return 1;
    }

    printf("start timeMinHeap\n");

    const char *ip = argv[1];
    int port = atoi(argv[2]);

    int ret = 0;

    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int reuse = 1;
    setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

    ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret != -1);

    ret = listen(listenfd, 5);
    assert(ret != -1);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd != -1);
    addfd(epollfd, listenfd);

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    setnoblocking(pipefd[1]);

    addfd(epollfd, pipefd[0]);

    addsig(SIGALRM);
    addsig(SIGTERM);

    bool stop_server = false;

    client_data *users = new client_data[FD_LIMIT];
    bool timeout = false;
    alarm(TIMESLOT);

    while (!stop_server)
    {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR))
        {
            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd)
            {
                struct sockaddr_in client_address;
                socklen_t client_addLength = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addLength);
                addfd(epollfd, connfd);

                heap_timer *pTimer = new heap_timer(3*TIMESLOT);
                pTimer->cb_func = cb_func;
                pTimer->user_data = &users[sockfd];

                users[connfd].sockfd = connfd;
                users[connfd].timer = pTimer;
                users[connfd].address = client_address;
                
                timeHeap->add_timer(pTimer);

                printf("receive sockfd %d connect\n",connfd);

            }
            else if(sockfd == pipefd[0] && (events[i].events & EPOLLIN))
            {
                int sig;
                char signals[1024];
                ret = recv(pipefd[0],signals,sizeof(signals),0);
                if(ret == -1 || ret == 0)
                {
                    continue;
                }
                else 
                {
                    for(int i = 0; i< ret; ++i)
                    {
                        switch(signals[i])
                        {
                            case SIGALRM:
                            {
                                timeout = true;
                                break;
                            }
                            case SIGTERM:
                            {
                                stop_server = true;
                                break;
                            }
                        }
                    }
                }
            }
            else if(events[i].events & EPOLLIN)
            {
                memset(users[sockfd].buff,'\0',BUFFER_SIZE);
                ret = recv(sockfd,users[sockfd].buff,BUFFER_SIZE-1,0);
                heap_timer *timer = users[sockfd].timer;

                printf("get %d bytes of client data %s from %d timer %p\n",ret,users[sockfd].buff,sockfd,timer);

                

                if(ret < 0)
                {
                    if(errno != EAGAIN)
                    {
                        cb_func(&users[sockfd]);
                        if(timer)
                        {
                            timeHeap->del_timer(timer);
                        }

                    }
                }
                else if (ret == 0)
                {
                    cb_func(&users[sockfd]);
                    if (timer)
                    {
                         timeHeap->del_timer(timer);
                    }
                }
                else
                {
                    printf("enter recv else timer %p\n",timer);
                    if (timer)
                    {
                        heap_timer *pTimer =new heap_timer(3 * TIMESLOT);
                        
                        pTimer->cb_func = cb_func;
                        pTimer->user_data = &users[sockfd];
                        users[sockfd].timer = pTimer;

                        timeHeap->del_timer(timer);
                        timeHeap->add_timer(pTimer);
                    }
                }
            }
        }

        if(timeout)
        {
            timer_handler();
            timeout = false;
        }
    }

    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete []users;
    return 0;
}


#endif