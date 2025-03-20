//
// Created by 86183 on 2025/3/13.
//
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include "logger.h"

#include <cstring>
#include <stdarg.h>

Logger::Logger() {
    file_ = nullptr;
    block_queue_ = nullptr;
    write_thread_ = nullptr;
    line_cnt_ = 0;
    today_ = 0;
    is_open_ = false;
    is_async_ = false;
}

Logger::~Logger() {
    if (is_async_) {
        while (!block_queue_->empty()) {
            block_queue_->flush();
        }
        block_queue_->close();
        // 等待写线程完成
        write_thread_->join();
    }
    if (file_ != nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        flush();
        fclose(file_);
    }
}
// 实际的写线程在做的事情, 不断的从阻塞队列中取数据并放到文件里
void Logger::asyncWriteLog() {
    std::string msg;
    while (block_queue_->pop(msg)) {
        std::lock_guard<std::mutex> lock(mutex_);
        fputs(msg.c_str(), file_);
    }
}
// 添加日志等级
void Logger::appendLogLevelTitle(int level) {
    switch (level) {
        case 0:
            buffer_.append("[debug]: ", 9);
            break;
        case 1:
            buffer_.append("[info]: ", 8);
            break;
        case 2:
            buffer_.append("[warn]: ", 8);
            break;
        case 3:
            buffer_.append("[error]: ", 9);
            break;
        default:
            buffer_.append("[info]: ", 8);
            break;
    }
}

void Logger::initLogger(int level, const char *path, const char *suffix, int max_queue_capcity) {
    is_open_ = true;
    level_ = level;
    path_ = path;
    suffix_ = suffix;
    assert(max_queue_capcity >= 0);
    if (max_queue_capcity > 0) {
        // 开启异步日志
        is_async_ = true;
        if (block_queue_ == nullptr) {
            // c++ 17 使用make_unique
            block_queue_ = std::make_unique<BlockQueue<std::string>>(max_queue_capcity);
            write_thread_ = std::make_unique<std::thread>(Logger::flushLogThread);
        }

    } else {
        is_async_ = false;
    }
    line_cnt_ = 0;
    time_t timer = time(nullptr);
    struct tm *tm = localtime(&timer);
    char filename[LOG_NAME_LEN] = {};
    // path_ + year(4位) + month(2位) + day(2位) + .log
    snprintf(filename, LOG_NAME_LEN - 1, "%s%04d_%02d_%02d%s",
        path_, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, suffix_);
    today_ = tm->tm_mday;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 清空buffer
        buffer_.retrieveAll();
        // 如果文件已经打开了, 写入最后一条, 关闭
        if (file_) {
            flush();
            fclose(file_);
        }
        // 重新打开文件, 没有就创建(append 追加模式)
        file_ = fopen(filename, "a");
        if (file_ == nullptr) {
            mkdir(path_, 0777);
            file_ = fopen(filename, "a");
        }
        assert(file_ != nullptr);
    }
}

Logger *Logger::getInstance() {
    static Logger instance;
    return &instance;
}
// 类静态函数, 调用唯一实例的异步写, 从block_queue中取出消息并写入
void Logger::flushLogThread() {
    getInstance()->asyncWriteLog();
}
// 宏的实际调用, 将log写入buffer中
void Logger::writeLog(int level, const char *format, ...) {
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);
    time_t t_seconds = now.tv_sec;
    struct tm tm = *localtime(&t_seconds);
    va_list args;
    // today_和line_cnt的访问本身应该加锁
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (today_ != tm.tm_mday || (line_cnt_ > 0 && line_cnt_ % MAX_LINES == 0)) {
            // std::unique_lock<std::mutex> lock(mutex_);

            char new_filename[LOG_NAME_LEN];
            char tail[36] = {0};
            snprintf(tail, 35, "%04d_%02d_%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);

            if (today_ != tm.tm_mday) {
                snprintf(new_filename, LOG_NAME_LEN - 72, "%s%s%s", path_, tail, suffix_);
                today_ = tm.tm_mday;
                line_cnt_ = 0;
            } else {
                snprintf(new_filename, LOG_NAME_LEN - 72, "%s%s-%d%s", path_, tail, (line_cnt_ / MAX_LINES), suffix_);
            }
            // lock.lock();
            flush();
            fclose(file_);
            file_ = fopen(new_filename, "a");
            assert(file_ != nullptr);
        }
    }
    {
        std::unique_lock<std::mutex> lock(mutex_);
        line_cnt_ += 1;
        // int n = snprintf(buffer_.beginWrite(), 128, "")
        char info[256] = {};
        snprintf(info, 128, "%d-%02d-%02d %02d:%02d:%02d.%06ld ",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec, now.tv_usec);
        std::string msg(info);
        buffer_.append(msg);

        appendLogLevelTitle(level);

        va_start(args, format);
        bzero(info, sizeof(info));
        int m = vsnprintf(info, 256, format, args);
        va_end(args);
        msg = std::string(info);
        buffer_.append(msg);
        buffer_.append("\n\0", 2);

        if (is_async_ && block_queue_!= nullptr && !block_queue_->full()) {
            block_queue_->push_back(buffer_.retrieveAllAsString());
        } else {
            fputs(buffer_.peek(), file_);
        }
        buffer_.retrieveAll();
    }
}
// 唤醒阻塞队列消费者
// 不是很清楚flush的作用, 写线程只有一个, queue的flush应该只能起到一个催促的作用
void Logger::flush() {
    if (is_async_) {
        block_queue_->flush();
    }
    fflush(file_);
}

int Logger::getLevel() {
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}

void Logger::setLevel(int level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

bool Logger::isOpen() {
    std::lock_guard<std::mutex> lock(mutex_);
    return is_open_;
}


