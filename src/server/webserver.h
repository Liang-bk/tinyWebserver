//
// Created by 86183 on 2025/4/6.
//

#ifndef WEBSERVER_H
#define WEBSERVER_H
#include <netinet/in.h>

#include "epoller.h"
#include "http/http_conn.h"
#include "pool/threadpool.h"
#include "timer/heap_timer.h"


class WebServer {
public:
    WebServer(int port, int trigger_mode, int timeout_ms, bool opt_linger,
              int sql_port, const char* sql_user, const char* sql_pwd,
              const char* db_name, int sql_conn_num, int threadpool_num,
              bool open_log, int log_level, int log_que_size);
    ~WebServer();
    void start();
private:
    bool initSocket();
    void initEventMode(int trigger_mode);
    void addClient(int clnt_fd, sockaddr_in addr);

    void handleListen();
    void handleWrite(HttpConnection* client);
    void handleRead(HttpConnection* client);

    void sendError(int clnt_fd, const char* info);
    void extendTime(HttpConnection* client);
    void closeConnection(HttpConnection* client);

    void onRead(HttpConnection* client);
    void onWrite(HttpConnection* client);
    void onProcess(HttpConnection* client);

    static const int MAX_FD = 65536;
    static int setFdNonBlock(int fd);

    int port_;          // 服务器端口号
    bool open_linger_;  // 打开优雅关闭
    int timeout_ms_;    // 定时时间
    bool is_closed_;    // 服务器是否关闭
    int server_fd_;     // 服务器监听的fd
    char *src_dir_;      // 资源目录

    uint32_t listen_event_; // 服务器的监听事件
    uint32_t conn_event_;   // 接收后的连接事件

    std::unique_ptr<HeapTimer> timer_;      // 堆计时器
    std::unique_ptr<Threadpool> threadpool_;// 线程池
    std::unique_ptr<Epoller> epoller_;      // epoll封装
    std::unordered_map<int, HttpConnection> clients_;   // 客户端连接信息

};



#endif //WEBSERVER_H
