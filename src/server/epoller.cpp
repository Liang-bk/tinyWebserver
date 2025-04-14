//
// Created by 86183 on 2025/4/3.
//

#include "epoller.h"

#include <assert.h>
#include <unistd.h>

Epoller::Epoller(int max_events): events_(max_events) {
    epfd_ = epoll_create(1);
    assert(epfd_ > 0 && events_.capacity() > 0);
}

Epoller::~Epoller() {
    close(epfd_);
}

bool Epoller::addFd(int fd, uint32_t events) {
    if (fd < 0) return false;
    epoll_event ev = {};
    ev.events = events;
    ev.data.fd = fd;
    return 0 == epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev);
}

bool Epoller::modFd(int fd, uint32_t events) {
    if (fd < 0) return false;
    epoll_event ev = {};
    ev.events = events;
    ev.data.fd = fd;
    return 0 == epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev);
}

bool Epoller::delFd(int fd) {
    if (fd < 0) return false;
    epoll_event ev = {};
    ev.data.fd = fd;
    return 0 == epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, &ev);
}

int Epoller::wait(int timeout) {
    return epoll_wait(epfd_, &events_[0], static_cast<int>(events_.size()), timeout);
}

int Epoller::getEventFd(size_t i) const {
    assert(i < static_cast<size_t>(events_.size()));
    return events_[i].data.fd;
}

uint32_t Epoller::getEvents(size_t i) const {
    assert(i < static_cast<size_t>(events_.size()));
    return events_[i].events;
}
