//
// Created by 86183 on 2025/4/6.
//

#include "webserver.h"

WebServer::WebServer(int port, int trigger_mode, int timeout_ms, bool opt_linger,
    int sql_port, const char *sql_user, const char *sql_pwd,
    const char *db_name, int sql_conn_num, int threadpool_num,
    bool open_log, int log_level, int log_que_size):
    port_(port), open_linger_(opt_linger), timeout_ms_(timeout_ms),
    is_closed_(false), timer_(std::make_unique<HeapTimer>()),
    threadpool_(std::make_unique<Threadpool>(threadpool_num)),
    epoller_(std::make_unique<Epoller>()) {
    src_dir_ = getcwd(nullptr, 256); // 可执行文件工作路径, 限定最长256字符
    strncat(src_dir_, "/resources/", 16);
    // 初始化http连接数
    HttpConnection::user_count = 0;
    HttpConnection::SRC_DIR = src_dir_;

    // 初始化数据库连接池
    SQLConnPool::getInstance()->initConnPool("localhost", sql_port,
        sql_user, sql_pwd, db_name, sql_conn_num);
    // 初始化事件模式
    initEventMode(trigger_mode);

    // 初始化socket, 开启监听
    if (!initSocket()) {
        is_closed_ = true;
    }

    if (open_log) {
        // 初始化日志信息
        Logger::getInstance()->initLogger(log_level, "./log", ".log", log_que_size);
        if (is_closed_) {
            LOG_ERROR("=========== Server init error! ===========");
        } else {
            LOG_INFO("=========== Server init success ===========");
            LOG_INFO("Port: %d, OpenLinger: %s", port_, opt_linger ? "true" : "false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                (listen_event_ & EPOLLET ? "ET" : "LT"),
                (conn_event_ & EPOLLET ? "ET" : "LT"));
            LOG_INFO("LogSys level: %d", log_level);
            LOG_INFO("SRC_DIR: %s", HttpConnection::SRC_DIR);
            LOG_INFO("SQLConnPool num: %d, ThreadPool num: %d", sql_conn_num, threadpool_num);
        }
    }
}

WebServer::~WebServer() {
    close(server_fd_);
    is_closed_ = true;
    free(src_dir_);
    SQLConnPool::getInstance()->closeConnPool();
    // threadPool的关闭由threadPool本身的析构执行
}

void WebServer::start() {
    // epoll_wait的阻塞时间, -1表示不阻塞
    int timeout = -1;
    if (!is_closed_) {
        LOG_INFO("=========== Server start ===========");
    }
    while (!is_closed_) {
        if (timeout_ms_ > 0) {
            timeout = timer_->getNextTick();
        }
        int event_cnt = epoller_->wait(timeout);

        for (int i = 0; i < event_cnt; ++i) {
            int fd = epoller_->getEventFd(i);
            uint32_t events = epoller_->getEvents(i);
            // 服务器, 接收新连接
            if (fd == server_fd_) {
                handleListen();
            } else if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 客户端: 对方半关闭/挂起/错误
                assert(clients_.count(fd) > 0);
                closeConnection(&clients_[fd]);
            } else if (events & EPOLLIN) {
                assert(clients_.count(fd) > 0);
                handleRead(&clients_[fd]);
            } else if (events & EPOLLOUT) {
                assert(clients_.count(fd) > 0);
                handleWrite(&clients_[fd]);
            } else {
                LOG_ERROR("Server: Unexpected event");
            }
        }
    }
}

void WebServer::addClient(int clnt_fd, sockaddr_in addr) {
    assert(clnt_fd > 0);
    clients_[clnt_fd].init(clnt_fd, addr);
    if (timeout_ms_ > 0) {
        timer_->add(clnt_fd, timeout_ms_, std::bind(&WebServer::closeConnection, this, &clients_[clnt_fd]));
    }
    // 监听client的可读和其他事件
    epoller_->addFd(clnt_fd, EPOLLIN | conn_event_);
    setFdNonBlock(clnt_fd);
    LOG_INFO("Client[%d] in!", clnt_fd);
}

void WebServer::handleListen() {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    do {
        int clnt_fd = accept(server_fd_, (struct sockaddr *)&client_addr, &client_addr_len);
        // accept 返回错误(通常是没有新连接了)
        if (clnt_fd < 0) {
            return;
        } else if (HttpConnection::user_count >= MAX_FD) {
            sendError(clnt_fd, "Server busy!");
            LOG_WARN("Server has reach the limits");
            return;
        }
        addClient(clnt_fd, client_addr);
    } while (listen_event_ & EPOLLET);
}

bool WebServer::initSocket() {
    int ret;
    struct sockaddr_in addr;
    if (port_ > 65536 || port_ < 1024) {
        LOG_ERROR("Port: %d error!", port_);
        return false;
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);
    struct linger opt_linger = {};

    if (open_linger_) {
        // 优雅关闭: 直到所剩数据发送完毕或超时
        opt_linger.l_onoff = 1;
        opt_linger.l_linger = 1;
    }
    // 开启一个tcp套接字
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd_ < 0) {
        LOG_ERROR("Create socket error!");
        return false;
    }

    // 设置端口复用: 优雅关闭
    ret = setsockopt(server_fd_, SOL_SOCKET, SO_LINGER, &opt_linger, sizeof(opt_linger));
    if (ret < 0) {
        close(server_fd_);
        LOG_ERROR("Init linger error!");
        return false;
    }

    int optval = 1;
    // 设置端口复用: SO_REUSEADDR, 允许服务快速重启, 绕过TIME_WAIT状态
    ret = setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
    if (ret == -1) {
        LOG_ERROR("set socket setsockopt error !");
        close(server_fd_);
        return false;
    }

    ret = bind(server_fd_, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        LOG_ERROR("Bind Port: %d error!", port_);
        close(server_fd_);
        return false;
    }

    ret = listen(server_fd_, SOMAXCONN);
    if (ret < 0) {
        LOG_ERROR("Listen port: %d error!", port_);
        close(server_fd_);
        return false;
    }
    // EPOLLIN: 对应的文件描述符有读事件
    ret = epoller_->addFd(server_fd_, listen_event_ | EPOLLIN);
    if (ret == 0) {
        LOG_ERROR("Add listen event error!");
        close(server_fd_);
        return false;
    }
    // 非阻塞式(read/write之类的IO函数在读不到数据时会立刻返回)
    setFdNonBlock(server_fd_);
    LOG_INFO("Server port: %d", port_);
    return true;
}

void WebServer::sendError(int clnt_fd, const char *info) {
    assert(clnt_fd > 0);
    int ret = send(clnt_fd, info, strlen(info), 0);
    if (ret < 0) {
        LOG_WARN("send error to client[%d] error!", clnt_fd);
    }
    close(clnt_fd);
}

void WebServer::closeConnection(HttpConnection *client) {
    assert(client != nullptr);
    LOG_INFO("Client[%d] quit!", client->getFd());
    // 取消监听对应客户端描述符
    epoller_->delFd(client->getFd());
    client->close();

}

/// 延长客户端的超时时间, 根据timeout_ms_
/// @param client 客户端
void WebServer::extendTime(HttpConnection *client) {
    assert(client != nullptr);
    if (timeout_ms_ > 0) {
        timer_->adjust(client->getFd(), timeout_ms_);
    }
}

void WebServer::handleRead(HttpConnection *client) {
    // 接收到http请求
    assert(client != nullptr);
    // 延长该客户端的超时时间
    extendTime(client);
    // 让线程池去处理实际的http业务
    threadpool_->addTask(std::bind(&WebServer::onRead, this, client));
}

void WebServer::handleWrite(HttpConnection *client) {
    assert(client != nullptr);
    extendTime(client);

    threadpool_->addTask(std::bind(&WebServer::onWrite, this, client));
}

void WebServer::initEventMode(int trigger_mode) {
    // EPOLLRDHUP: 半连接(对端半关闭连接事件)
    listen_event_ = EPOLLRDHUP;
    // EPOLLONESHOT: 处理完这个事件后, 将文件描述符自动从epoll空间去除
    conn_event_ = EPOLLONESHOT | EPOLLRDHUP;
    // EPOLLET: 将文件描述符设置为边缘(ET)触发模式, epoll监听到事件后只会通知一次
    switch (trigger_mode) {
        case 0:
            break;;
        case 1:
            conn_event_ |= EPOLLET;
            break;
        case 2:
            listen_event_ |= EPOLLET;
            break;
        case 3:
            listen_event_ |= EPOLLET;
            conn_event_ |= EPOLLET;
            break;
        default:
            listen_event_ |= EPOLLET;
            conn_event_ |= EPOLLET;
            break;
    }
    HttpConnection::isET = (conn_event_ & EPOLLET);

}

void WebServer::onProcess(HttpConnection *client) {
    if (client->process()) {
        epoller_->modFd(client->getFd(), conn_event_ | EPOLLOUT);
    } else {
        epoller_->modFd(client->getFd(), conn_event_ | EPOLLIN);
    }
}

void WebServer::onRead(HttpConnection *client) {
    assert(client != nullptr);
    int ret = -1;
    int read_errno = 0;
    // 一次性把系统缓冲区中数据读出
    ret = client->read(&read_errno);
    // 读出错
    if (ret <= 0 && read_errno != EAGAIN) {
        closeConnection(client);
        return;
    }
    // 业务逻辑处理
    onProcess(client);
}

void WebServer::onWrite(HttpConnection *client) {
    assert(client != nullptr);
    int ret = -1;
    int write_errno = 0;
    ret = client->write(&write_errno);

    if (client->toWriteBytes() == 0) {
        if (client->isKeepAlive()) {
            onProcess(client);
            return;
        }
    } else if (ret < 0) {
        if (write_errno == EAGAIN) {
            epoller_->modFd(client->getFd(), conn_event_ | EPOLLOUT);
            return;
        }
    }
    closeConnection(client);
}

/// 将文件描述符fd设置为非阻塞
/// @param fd 文件描述符
/// @return 设置是否成功, < 0表示设置失败
int WebServer::setFdNonBlock(int fd) {
    assert(fd > 0);
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD) | O_NONBLOCK);
}

