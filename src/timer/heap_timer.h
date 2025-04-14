//
// Created by 86183 on 2025/4/1.
//

#ifndef HEAP_TIMER_H
#define HEAP_TIMER_H
#pragma once
#include <chrono>
#include <functional>
#include "logger/logger.h"

using TimeoutCallBack = std::function<void()>;
using exact_clock = std::chrono::high_resolution_clock;
using ms = std::chrono::milliseconds;
using time_stamp = exact_clock::time_point;

struct TimerNode {
    int fd; // 套接字标识
    time_stamp expires; // 连接过期时间
    TimeoutCallBack cb; // 过期后要执行的函数
    bool operator <(const TimerNode &other) const {
        // 过期时间越短, 越早被处理
        return expires < other.expires;
    }
};
// 小根计时器堆
class HeapTimer {
public:
    HeapTimer() {
        timer_nodes_.reserve(64);
        // 占住0号位, 方便后续操作
        timer_nodes_.push_back({});
    }

    ~HeapTimer() { clear(); }

    // 调整fd的连接超时时间为timeout
    void adjust(int fd, int timeout);

    // 添加fd的连接超时时间为timeout, 回调函数为cb
    void add(int fd, int timeout, const TimeoutCallBack &cb);

    // 删除fd结点
    void doWork(int fd);

    // 清空堆
    void clear();

    // 清除超时结点
    void tick();

    // 清除堆顶结点
    void pop();

    // 距离最近的超时连接还剩余多少时间
    int getNextTick();

private:
    // 删除一个结点
    void deleteNode(size_t i);

    // 结点上升
    void shiftUp(size_t i);

    // 结点下降
    void shiftDown(size_t i);

    // 结点交换
    void swapNode(size_t i, size_t j);
    // 获取堆大小
    size_t size() const {
        // timer_nodes的size至少为1
        return timer_nodes_.size() - 1;
    }

    // timer结点
    std::vector<TimerNode> timer_nodes_;
    // 套接字fd到vector下标映射
    std::unordered_map<int, size_t> fd_to_index_;
};

#endif //HEAP_TIMER_H
