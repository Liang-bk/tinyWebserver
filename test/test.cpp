//
// Created by 86183 on 2025/3/21.
//
#include <src/logger/logger.h>
#include <src/pool/threadpool.h>
#include <features.h>

#if __GLIBC__ == 2 && __GLIBC_MINOR__ < 30
#include <sys/syscall.h>
#define gettid() syscall(SYS_gettid)
#endif

void testLogger() {
    int cnt = 0, level = 0;
    Logger::getInstance()->initLogger(level, "./testlog1", ".log", 0);
    for(level = 3; level >= 0; level--) {
        Logger::getInstance()->setLevel(level);
        for(int j = 0; j < 10000; j++ ){
            for(int i = 0; i < 4; i++) {
                LOG_BASE(i,"%s 111111111 %d ============= ", "Test", cnt++);
            }
        }
    }
    cnt = 0;
    Logger::getInstance()->initLogger(level, "./testlog2", ".log", 5000);
    for(level = 0; level < 4; level++) {
        Logger::getInstance()->setLevel(level);
        for(int j = 0; j < 10000; j++ ){
            for(int i = 0; i < 4; i++) {
                LOG_BASE(i,"%s 222222222 %d ============= ", "Test", cnt++);
            }
        }
    }
}

void threadLoggerTask(int i, int cnt) {
    for(int j = 0; j < 10000; j++ ){
        LOG_BASE(i,"PID:[%04d]======= %05d ========= ", std::this_thread::get_id(), cnt++);
    }
}

void testThreadPool() {
    Logger::getInstance()->initLogger(0, "./testThreadpool", ".log", 5000);
    Threadpool threadpool(6);
    for(int i = 0; i < 18; i++) {
        threadpool.addTask(std::bind(threadLoggerTask, i % 4, i * 10000));
    }
    getchar();
}

int main() {
    //testLogger();
    testThreadPool();
    return 0;
}