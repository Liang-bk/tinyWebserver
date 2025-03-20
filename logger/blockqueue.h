//
// Created by 86183 on 2025/3/13.
//

#ifndef BLOCKQUEUE_H
#define BLOCKQUEUE_H
#pragma once
#include <assert.h>
#include <condition_variable>
#include <deque>
#include <mutex>

template <typename T>
class BlockQueue {
public:
    explicit BlockQueue(size_t max_capacity = 1000);
    ~BlockQueue();

    void clear();
    bool empty();
    bool full();

    void close();

    size_t size();
    size_t capacity();

    T front();
    T back();

    void push_front(T item);
    void push_back(T item);

    bool pop(T &item);
    bool pop(T &item, int timeout);

    void flush();
private:
    std::deque<T> deque_;
    std::mutex mutex_;
    size_t capacity_;
    bool is_close_;
    std::condition_variable producer_cond_;
    std::condition_variable consumer_cond_;
};

template<typename T>
BlockQueue<T>::BlockQueue(size_t max_capacity) : capacity_(max_capacity) {
    // 一般是异步日志
    assert(max_capacity > 0);
    is_close_ = false;
}

template<typename T>
BlockQueue<T>::~BlockQueue() {
    close();
}

template<typename T>
void BlockQueue<T>::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    deque_.clear();
}

template<typename T>
bool BlockQueue<T>::empty() {
    std::lock_guard<std::mutex> lock(mutex_);
    return deque_.empty();
}

template<typename T>
bool BlockQueue<T>::full() {
    std::lock_guard<std::mutex> lock(mutex_);
    return deque_.size() == capacity_;
}

template<typename T>
void BlockQueue<T>::close() {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        deque_.clear();
        is_close_ = true;
    }
    consumer_cond_.notify_all();
    producer_cond_.notify_all();
}

template<typename T>
size_t BlockQueue<T>::size() {
    std::lock_guard<std::mutex> lock(mutex_);
    return deque_.size();
}

template<typename T>
size_t BlockQueue<T>::capacity() {
    std::lock_guard<std::mutex> lock(mutex_);
    return capacity_;
}

template<typename T>
T BlockQueue<T>::front() {
    std::lock_guard<std::mutex> lock(mutex_);
    return deque_.front();
}

template<typename T>
T BlockQueue<T>::back() {
    std::lock_guard<std::mutex> lock(mutex_);
    return deque_.back();
}

template<typename T>
void BlockQueue<T>::push_front(T item) {
    std::unique_lock<std::mutex> lock(mutex_);
    // 生产者生产一条消息
    producer_cond_.wait(lock, [this]() {
        return deque_.size() < capacity_ || is_close_;
    });
    if (is_close_) {
        return;
    }
    deque_.push_front(item);
    consumer_cond_.notify_one();
}

template<typename T>
void BlockQueue<T>::push_back(T item) {
    std::unique_lock<std::mutex> lock(mutex_);
    producer_cond_.wait(lock, [this]() {
        return deque_.size() < capacity_ || is_close_;
    });
    if (is_close_) {
        return;
    }
    deque_.push_back(item);
    consumer_cond_.notify_one();
}

template<typename T>
bool BlockQueue<T>::pop(T &item) {
    // 取出一条数据
    std::unique_lock<std::mutex> lock(mutex_);
    consumer_cond_.wait(lock, [this]() {
        return !deque_.empty() || is_close_;
    });
    if (is_close_) {
        return false;
    }
    item = deque_.front();
    deque_.pop_front();
    producer_cond_.notify_one();
    return true;
}

template<typename T>
bool BlockQueue<T>::pop(T &item, int timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (consumer_cond_.wait_for(lock, std::chrono::seconds(timeout),[this]() {
        return !deque_.empty() || is_close_;
    }) == false) {
        return false;
    }
    if (is_close_) {
        return false;
    }
    item = deque_.front();
    deque_.pop_front();
    producer_cond_.notify_one();
    return true;
}

template<typename T>
void BlockQueue<T>::flush() {
    consumer_cond_.notify_one();
}

#endif //BLOCKQUEUE_H
