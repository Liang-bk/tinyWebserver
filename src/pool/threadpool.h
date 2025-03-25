//
// Created by 86183 on 2025/3/20.
//
#pragma once
#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <assert.h>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

class Threadpool {
public:
    Threadpool() = default;
    Threadpool(Threadpool &&) = default;
    explicit Threadpool(size_t thread_count = std::thread::hardware_concurrency()) {
        pool_ = std::make_shared<Pool>();
        assert(thread_count > 0);
        for (size_t i = 0; i < thread_count; ++i) {
            std::thread([this]() {
                std::unique_lock<std::mutex> lock(this->pool_->mutex);
                while (true) {
                    if (!pool_->tasks.empty()) {
                        auto task = std::move(this->pool_->tasks.front());
                        this->pool_->tasks.pop();
                        lock.unlock();
                        task();
                        // 要访问队列取新任务, 先试图上锁
                        lock.lock();
                    } else if (this->pool_->is_close) {
                        break;
                    } else {
                        // 如果为空, 阻塞自己等待唤醒
                        this->pool_->condition.wait(lock);
                    }
                }
            }).detach();
        }
    }
    ~Threadpool() {
        if (pool_) {
            std::unique_lock<std::mutex> lock(pool_->mutex);
            pool_->is_close = true;
        }
        pool_->condition.notify_all();
    }
    template<typename T>
    void addTask(T &&task) {
        // T &&task 如果T是左值, 传进来的就是 T &task(左值的引用)
        //          如果T是右值, 传进来的就是 T task(普通类型)
        std::unique_lock<std::mutex> lock(pool_->mutex);
        // forward保证了右值不会因为传进来的是T task而改变其右值属性
        // 比如外部调用一个addTask([](){})的形式, 传入之后就变成了 function<void()> task,
        // 这是一个左值, forward的作用就是task原来是右值, 现在还是右值
        // emplace 在处理右值的时候直接constructor, 其余和push相同
        pool_->tasks.emplace(std::forward<T>(task));
        // 来了一个任务, 唤醒一个线程处理
        pool_->condition.notify_one();
    }
private:
    struct Pool {
        std::mutex mutex;
        std::condition_variable condition;
        std::queue<std::function<void()>> tasks;
        bool is_close;
    };
    std::shared_ptr<Pool> pool_;
};
#endif //THREADPOOL_H
