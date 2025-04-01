//
// Created by 86183 on 2025/3/30.
//

#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <sys/types.h>
#include <sys/uio.h>    // readv, writev
#include <arpa/inet.h>  // sockaddr_in
#include <stdlib.h>     // atoi()
#include <errno.h>

#include "logger/logger.h"
#include "pool/sqlconnpool.h"
#include "buffer/buffer.h"
#include "http_request.h"
#include "http_response.h"

class HttpConnection {
public:
    HttpConnection();
    ~HttpConnection();
    void init(int sock_fd, const sockaddr_in &addr);
    ssize_t read(int *save_errno);
    ssize_t write(int *save_errno);
    void close();
    int getFd() const;
    int getPort() const;
    const char *getIp() const;
    sockaddr_in getAddress() const;
    bool process();
    int toWriteBytes() {
        // 两块内存大小加起来就是要写入fd的大小
        return iov_[0].iov_len + iov_[1].iov_len;
    }
    bool isKeepAlive() const {
        return request_.isKeepAlive();
    }
    static bool isET;
    static const char* SRC_DIR;
    static std::atomic<int> user_count;
private:
    int sock_fd_;
    sockaddr_in addr_;
    bool is_close_;
    int iov_cnt_;
    struct iovec iov_[2];

    Buffer read_buffer_;
    Buffer write_buffer_;

    HttpRequest request_;
    HttpResponse response_;
};



#endif //HTTP_CONN_H
