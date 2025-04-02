//
// Created by 86183 on 2025/4/1.
//

#include "heap_timer.h"
void HeapTimer::adjust(int fd, int timeout) {
    size_t heap_size = size();
    assert(heap_size > 0 && fd_to_index_.contains(fd) > 0);
    size_t index = fd_to_index_[fd];
    timer_nodes_[index].expires = exact_clock::now() + ms(timeout);
    shiftDown(index);
}

void HeapTimer::add(int fd, int timeout, const TimeoutCallBack &cb) {
    assert(fd >= 0);
    if (!fd_to_index_.contains(fd)) {
        // 新client, 先插入堆底, 上升
        timer_nodes_.emplace_back(fd, exact_clock::now() + ms(timeout), cb);
        size_t heap_size = size();
        fd_to_index_[fd] = heap_size;
        shiftUp(heap_size);
    } else {
        // 旧client
        size_t index = fd_to_index_[fd];
        timer_nodes_[index].expires = exact_clock::now() + ms(timeout);
        timer_nodes_[index].cb = cb;
        size_t heap_size = size();
        // 先尝试上升
        shiftUp(index);
        if (index == fd_to_index_[fd]) {
            // 没升, 尝试下降
            shiftDown(index);
        }
    }
}

void HeapTimer::doWork(int fd) {
    // 删除fd, 触发回调函数
    size_t heap_size = size();
    if (heap_size == 0 || !fd_to_index_.contains(fd)) {
        return;
    }
    size_t index = fd_to_index_[fd];
    TimerNode node = timer_nodes_[index];
    node.cb();
    deleteNode(index);
}

void HeapTimer::clear() {
    timer_nodes_.clear();
    timer_nodes_[0] = {};
    fd_to_index_.clear();
}

void HeapTimer::tick() {
    if (size() == 0) {
        return;
    }
    while (size() > 0) {
        TimerNode node = timer_nodes_[1];
        if (std::chrono::duration_cast<ms>(node.expires - exact_clock::now()).count() > 0) {
            break;
        }
        node.cb();
        pop();
    }
}

void HeapTimer::pop() {
    size_t heap_size = size();
    assert(heap_size > 0);
    deleteNode(1);
}

int HeapTimer::getNextTick() {
    tick();
    // unsigned long, 不能用负数初始化
    ssize_t res = 0;
    if (size() > 0) {
        res = std::chrono::duration_cast<ms>(timer_nodes_[1].expires - exact_clock::now()).count();
        if (res < 0) {
            res = 0;
        }
    }
    return res;
}

void HeapTimer::deleteNode(size_t i) {
    size_t heap_size = size();
    assert(i >= 1 && i <= heap_size);
    size_t last = heap_size;
    swapNode(i, last);
    fd_to_index_.erase(timer_nodes_[last].fd);
    timer_nodes_.pop_back();
    // i与last结点交换位置了, 现在应该下降i
    shiftDown(i);
}

void HeapTimer::shiftUp(size_t i) {
    size_t heap_size = size();
    assert(i >= 1 && i <= heap_size);
    while (i > 1 && timer_nodes_[i] < timer_nodes_[i / 2]) {
        swapNode(i, i / 2);
        i /= 2;
    }
}

void HeapTimer::shiftDown(size_t i) {
    size_t heap_size = size();
    assert(i >= 1 && i <= heap_size);

    while (i <= heap_size) {
        // 比两次, 左结点, 右节点
        size_t i_copy = i;
        size_t left = 2 * i;
        size_t right = left + 1;
        if (left <= heap_size && timer_nodes_[left] < timer_nodes_[i]) {
            i = left;
        }
        if (right <= heap_size && timer_nodes_[right] < timer_nodes_[i]) {
            i = right;
        }
        if (i_copy != i) {
            swapNode(i, i_copy);
        } else {
            break;
        }
    }
}

void HeapTimer::swapNode(size_t i, size_t j) {
    // swap the timer_nodes[i] and timer_nodes[j]
    size_t heap_size = size();
    assert(i >= 1 && i <= heap_size);
    assert(j >= 1 && j <= heap_size);
    std::swap(timer_nodes_[i], timer_nodes_[j]);
    fd_to_index_[timer_nodes_[i].fd] = i;
    fd_to_index_[timer_nodes_[j].fd] = j;
}
