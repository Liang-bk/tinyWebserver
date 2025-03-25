//
// Created by 86183 on 2025/3/20.
//
#pragma once
#ifndef SQLCONNPOOL_H
#define SQLCONNPOOL_H
#include <mutex>
#include <queue>
#include <mysql/mysql.h>
#include <semaphore.h>
// SQL连接池, 用于控制SQL连接, 多个外部访问需要访问数据库时方便直接分配连接
// 访问完毕后释放以达到复用的效果
class SQLConnPool {
public:
    // 单例模式, 全局保留一个实例
    static SQLConnPool* getInstance();
    // 从池中取出一个SQL连接
    MYSQL *getConn();
    // 使用完毕后将该SQL连接放回池
    void freeConn(MYSQL *sql);
    // 池中可用的SQL连接
    int getFreeConnCount();

    /// 初始化连接池
    /// @param host  地址
    /// @param port  端口
    /// @param user  用户名
    /// @param pwd   密码
    /// @param db_name 数据库名称
    /// @param conn_size 连接数量
    void initConnPool(const char *host, int port,
                      const char *user, const char *pwd,
                      const char *db_name, int conn_size);
    // 关闭连接池
    void closeConnPool();
private:
    // 构造函数私有实现
    SQLConnPool();
    ~SQLConnPool();

    // 最大连接数
    int MAX_CONN_;
    // 池队列
    std::queue<MYSQL *> conn_que_;
    std::mutex mutex_;
    // 信号量
    sem_t sem_id_;
};
#endif //SQLCONNPOOL_H
