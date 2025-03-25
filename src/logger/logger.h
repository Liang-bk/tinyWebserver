//
// Created by 86183 on 2025/3/13.
//

#ifndef LOGGER_H
#define LOGGER_H

#include <memory>
#include <queue>
#include <thread>

#include "blockqueue.h"
#include "buffer/buffer.h"

class Logger {
public:
    /// 初始化Logger设置
    /// @param level 日志等级
    /// @param path 日志存储路径
    /// @param suffix 日志文件后缀
    /// @param max_queue_capcity 阻塞队列能存放的最大消息数量
    void initLogger(int level, const char* path = "./log",
                    const char* suffix = ".log", int max_queue_capcity = 1024);
    static Logger* getInstance();
    // 异步写日志
    static void flushLogThread();
    // 按照标准格式写日志
    void writeLog(int level, const char* format, ...);
    void flush();

    int getLevel();
    void setLevel(int level);
    bool isOpen();
private:
    // 构造函数放在private域, 保证全局只有一个实例
    Logger();
    virtual ~Logger();
    // 添加日志等级
    void appendLogLevelTitle(int level);
    // 实际的异步写日志
    void asyncWriteLog();

    static const int LOG_PATH_LEN = 256;
    static const int LOG_NAME_LEN = 256;
    static const int MAX_LINES = 50000; // 单个日志文件的最大条数

    const char* path_{};
    const char* suffix_{};

    int line_cnt_;      // 已写入的条数
    int today_;         // 当前日期
    bool is_async_;     // 是否开启异步写
    bool is_open_;      // logger是否开启
    int level_{};         // 日志等级

    Buffer buffer_;
    FILE* file_;        // 写入文件的指针
    std::unique_ptr<BlockQueue<std::pair<FILE*, std::string>>> block_queue_;
    std::unique_ptr<std::thread> write_thread_;
    std::mutex mutex_;
};

#define LOG_BASE(level, format, ...) \
    do { \
        Logger* logger = Logger::getInstance(); \
        if (logger->isOpen() && logger->getLevel() <= level) { \
            logger->writeLog(level, format, ##__VA_ARGS__); \
            logger->flush(); \
        } \
    } while(0);

#define LOG_DEBUG(format, ...) do {LOG_BASE(0, format, ##__VA_ARGS__); } while(0);
#define LOG_INFO(format, ...) do {LOG_BASE(1, format, ##__VA_ARGS__); } while(0);
#define LOG_WARN(format, ...) do {LOG_BASE(2, format, ##__VA_ARGS__); } while(0);
#define LOG_ERROR(format, ...) do {LOG_BASE(3, format, ##__VA_ARGS__); } while(0);
#endif //LOGGER_H
