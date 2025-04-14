//
// Created by 86183 on 2025/3/30.
//

#include "http_conn.h"
const char* HttpConnection::SRC_DIR;
std::atomic<int> HttpConnection::user_count;
// ET: 事件发生时, 只通知一次
bool HttpConnection::isET = true;

HttpConnection::HttpConnection() {
    sock_fd_ = -1;
    addr_ = {};
    is_close_ = true;
}

HttpConnection::~HttpConnection() {
    close();
}

void HttpConnection::init(int sock_fd, const sockaddr_in &addr) {
    assert(sock_fd > 0);
    user_count += 1;
    sock_fd_ = sock_fd;
    addr_ = addr;
    // 清空读写缓冲
    read_buffer_.retrieveAll();
    write_buffer_.retrieveAll();
    is_close_ = false;
    LOG_INFO("Client[%d](%s:%d) in, user_count: %d", sock_fd_, getIp(), getPort(), static_cast<int>(user_count));
}

void HttpConnection::close() {
    // 取消文件到内存的映射
    response_.unmapFile();
    if (is_close_ == false) {
        is_close_ = true;
        user_count -= 1;
        // 关闭套接字
        ::close(sock_fd_);
        LOG_INFO("Client[%d](%s:%d) quit, user_count: %d", sock_fd_, getIp(), getPort(), static_cast<int>(user_count));
    }
}

int HttpConnection::getFd() const {
    return sock_fd_;
}

sockaddr_in HttpConnection::getAddress() const {
    return addr_;
}

const char *HttpConnection::getIp() const {
    // network to ascii
    return inet_ntoa(addr_.sin_addr);
}

int HttpConnection::getPort() const {
    // network to host short
    return ntohs(addr_.sin_port);
}

ssize_t HttpConnection::read(int *save_errno) {
    // ET模式下, 需一次性读出所有数据(ET事件只触发一次)
    ssize_t len = -1;
    do {
        // 将sock_fd中的数据读入buffer中
        len = read_buffer_.readFd(sock_fd_, save_errno);
        // 读完标志
        if (len <= 0) {
            break;
        }
    } while (isET);
    return len;
}

ssize_t HttpConnection::write(int *save_errno) {
    ssize_t len = -1;
    do {
        len = writev(sock_fd_, iov_, iov_cnt_);
        if (len <= 0) {
            *save_errno = errno;
            break;
        }
        // 两块都写完毕
        if (iov_[0].iov_len + iov_[1].iov_len == 0) {
            break;
        } else if (static_cast<size_t>(len) > iov_[0].iov_len) {
            // 写完了第一块, 开始写第二块
            // 1. 第一次写就写完了iov_[0]
            // 2. 后续写才写完
            iov_[1].iov_base = (uint8_t *)iov_[1].iov_base + (len - iov_[0].iov_len);
            iov_[1].iov_len -= (len - iov_[0].iov_len);
            if (iov_[0].iov_len > 0) {
                write_buffer_.retrieveAll();
                iov_[0].iov_len = 0;
            }
        } else {
            // 第一块还没写完
            iov_[0].iov_base = (uint8_t *)iov_[0].iov_base + len;
            iov_[0].iov_len -= len;
            write_buffer_.retrieve(len);
        }

    } while (isET || toWriteBytes() > 10240);   // 10kb
    return len;
}

bool HttpConnection::process() {
    // 初始化请求
    request_.init();
    if (read_buffer_.readableBytes() <= 0) {
        return false;
    } else if (request_.parse(read_buffer_)) {
        LOG_DEBUG("%s", request_.path().c_str());
        response_.init(SRC_DIR, request_.path(), request_.isKeepAlive(), 200);
    } else {
        response_.init(SRC_DIR, request_.path(), false, 400);
    }
    // 将response写到write_buffer_
    response_.makeResponse(write_buffer_);
    iov_[0].iov_base = const_cast<char *>(write_buffer_.peek());
    iov_[0].iov_len = write_buffer_.readableBytes();
    iov_cnt_ =  1;
    // 有文件要传输的话, 额外传输
    if (response_.getFile() != nullptr && response_.getFileSize() > 0) {
        iov_[1].iov_base = response_.getFile();
        iov_[1].iov_len = response_.getFileSize();
        iov_cnt_ = 2;
    }
    LOG_DEBUG("file size: %d, %d to %d", response_.getFileSize(), iov_cnt_, toWriteBytes());
    return true;
}
