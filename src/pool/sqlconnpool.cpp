//
// Created by 86183 on 2025/3/20.
//

#include "sqlconnpool.h"

#include <assert.h>

#include "logger/logger.h"

SQLConnPool::SQLConnPool() {
}

SQLConnPool::~SQLConnPool() {
    closeConnPool();
}

SQLConnPool *SQLConnPool::getInstance() {
    static SQLConnPool instance;
    return &instance;
}

void SQLConnPool::initConnPool(const char *host, int port,
    const char *user, const char *pwd,
    const char *db_name, int conn_size) {
    assert(conn_size > 0);
    for (int i = 0; i < conn_size; ++i) {
        MYSQL *sql_conn = nullptr;
        sql_conn = mysql_init(sql_conn);
        if (!sql_conn) {
            LOG_ERROR("MYSQL init error!");
            assert(sql_conn);
        }
        sql_conn = mysql_real_connect(sql_conn, host,
            user, pwd, db_name,
            port, nullptr, 0);

        if (!sql_conn) {
            LOG_ERROR("MYSQL connect error!");
        }
        conn_que_.push(sql_conn);
    }
    MAX_CONN_ = conn_size;
    sem_init(&sem_id_, 0, MAX_CONN_);
}

MYSQL *SQLConnPool::getConn() {
    MYSQL *sql = nullptr;
    if (conn_que_.empty()) {
        LOG_WARN("SQLConnPool busy!");
        return nullptr;
    }
    sem_wait(&sem_id_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sql = conn_que_.front();
        conn_que_.pop();
    }
    return sql;
}

void SQLConnPool::freeConn(MYSQL *sql) {
    assert(sql);
    std::lock_guard<std::mutex> lock(mutex_);
    conn_que_.push(sql);
    sem_post(&sem_id_);
}

int SQLConnPool::getFreeConnCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    return conn_que_.size();
}

void SQLConnPool::closeConnPool() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!conn_que_.empty()) {
        auto item = conn_que_.front();
        conn_que_.pop();
        mysql_close(item);
    }
    mysql_library_end();
}