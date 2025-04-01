//
// Created by 86183 on 2025/3/20.
//
#pragma once
#ifndef SQLCONNRALL_H
#define SQLCONNRALL_H
#include <assert.h>
#include <mysql/mysql.h>

#include "sqlconnpool.h"
/*
 * 资源在对象构造初始化, 资源在对象析构时释放
 * 作为线程池对外的接口存在, 申请以及销毁资源
 */
class SQLConnRAll {
public:
    /// 获取一个SQL连接
    /// @param sql 二级指针是为了改变一级指针指向的位置
    /// @param conn_pool 资源获取地(从池中获取数据)
    SQLConnRAll(MYSQL **sql, SQLConnPool *conn_pool) {
        assert(conn_pool);
        *sql = conn_pool->getConn();
        sql_ = *sql;
        conn_pool_ = conn_pool;
    }
    ~SQLConnRAll() {
        if (sql_) {
            conn_pool_->freeConn(sql_);
        }
    }
private:
    MYSQL *sql_;
    SQLConnPool *conn_pool_;
};
#endif //SQLCONNRALL_H
