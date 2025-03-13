//
// Created by 86183 on 2025/3/11.
//

#include "buffer.h"
#include <sys/uio.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
/*
 * Buffer应该提供的两个最重要的功能:
 * 1. 从sockfd中读
 * 2. 向sockfd中写
 */
const size_t Buffer::kInitialSize;

Buffer::Buffer(size_t initial_size) :buffer_(initial_size),
                                    read_pos_(0),
                                    write_pos_(0) {
}
// 可读的数据量
size_t Buffer::readableBytes() const {
    return write_pos_ - read_pos_;
}
// 还能写入的数据量
size_t Buffer::writableBytes() const {
    return buffer_.size() - write_pos_;
}
// 预留空间, 已经读过的数据就没用了
size_t Buffer::prependableBytes() const {
    return read_pos_;
}


const char *Buffer::beginWrite() const {
    return begin() + write_pos_;
}

char *Buffer::beginWrite() {
    return begin() + write_pos_;
}

const char *Buffer::peek() const {
    return begin() + read_pos_;
}

void Buffer::retrieve(size_t len) {
    assert(len <= readableBytes());
    if (len < readableBytes()) {
        read_pos_ += len;
    } else {
        retrieveAll();
    }
}
// 取走[peek(), end)这段数据
void Buffer::retrieveUntil(const char *end) {
    assert(peek() <= end);
    assert(end <= (begin() + write_pos_));
    retrieve(end - peek());
}

void Buffer::retrieveAll() {
    read_pos_ = 0;
    write_pos_ = 0;
}

std::string Buffer::retrieveAllAsString() {
    std::string res(peek(), readableBytes());
    retrieveAll();
    return res;
}

void Buffer::append(const char *str, size_t len) {
    // TODO: 一定次数内未超过则缩小buffer空间
    ensureWritable(len);
    std::copy(str, str + len, beginWrite());
    hasWritten(len);
}

void Buffer::append(const std::string &str) {
    append(str.data(), str.size());
}

void Buffer::append(const void *data, size_t len) {
    append(static_cast<const char*>(data), len);
}

void Buffer::append(const Buffer &buff) {
    append(buff.peek(), buff.readableBytes());
}

ssize_t Buffer::readFd(int fd, int *err_flag) {
    // 能一次性读完吗? - 不能, 尽量的去读, 由更上层封装 while循环读取直到读完
    // 读到的数据放到buffer_的什么位置? - writePos及之后
    char extrabuf[65536];   // 64KB
    iovec vec[2] = {};
    const size_t writable = writableBytes();
    // why not use the beginWrite?
    // vec[0].iov_base = begin() + write_pos_;
    vec[0].iov_base = beginWrite();
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);

    const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;
    ssize_t len = readv(fd, vec, iovcnt);
    if (len < 0) {
        // read 系统调用错误
        *err_flag = errno;
    } else if (len <= writable) {
        // read读出的数据可以直接写到buffer_中, 直接移动指针
        write_pos_ += len;
    } else {
        // read读出了溢出的数据, 扩大buffer_, 等下一次读
        write_pos_ = buffer_.size();
        append(extrabuf, len - writable);
    }
    return len;
}