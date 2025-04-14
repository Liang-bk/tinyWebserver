//
// Created by 86183 on 2025/4/3.
//

#ifndef EPOLLER_H
#define EPOLLER_H
#pragma once

#include <vector>
#include <sys/epoll.h>

class Epoller {
public:
    explicit Epoller(int max_events = 1024);

    ~Epoller();

    // 向epoll空间内添加事件为events的套接字fd
    bool addFd(int fd, uint32_t events);

    // 将epoll空间内修改套接字fd的事件为events
    bool modFd(int fd, uint32_t events);

    // 将epoll空间内删除套接字fd
    bool delFd(int fd);

    // 等待事件发生, timeout=-1表示非阻塞等待, 返回发生事件的套接字数量
    int wait(int timeout = -1);

    // 获取下标为i的套接字
    int getEventFd(size_t i) const;

    // 获取下标为i的套接字对应的events
    uint32_t getEvents(size_t i) const;

private:
    int epfd_;
    std::vector<epoll_event> events_;
};


#endif //EPOLLER_H
