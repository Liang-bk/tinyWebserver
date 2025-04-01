//
// Created by 86183 on 2025/3/11.
//

#ifndef BUFFER_H
#define BUFFER_H
#include <string>
#include <vector>
#include <cassert>

class Buffer {
public:
    static const size_t kInitialSize = 1024;

    Buffer(size_t initial_size = kInitialSize);
    ~Buffer() = default;

    size_t readableBytes() const;
    size_t writableBytes() const;
    size_t prependableBytes() const;

    const char* beginWriteConst() const;
    char* beginWrite();
    const char* peek() const;

    // retrieve系列函数只涉及读指针的改变, 因为没有必要再转成char buffer[]再写到socket中
    void retrieve(size_t len);
    void retrieveUntil(const char *end);
    void retrieveAll();
    std::string retrieveAllAsString();

    void append(const char* str, size_t len);
    void append(const std::string& str);
    void append(const void* data, size_t len);
    void append(const Buffer& buff);

    ssize_t readFd(int fd, int *err_flag);
    // void writeFd(int fd, int *err_flag);
private:
    char* begin() {
        return &buffer_[0];
    }

    const char *begin() const {
        return &buffer_[0];
    }
    // 确保至少有len大小的可写空间
    void ensureWritable(size_t len) {
        if (len > writableBytes()) {
            expandSpace(len);
        }
        assert(len <= writableBytes());
    }
    // 扩大空间
    void expandSpace(size_t size) {
        if (writableBytes() + prependableBytes() < size) {
            // 如果剩余空间放不下size大小的数据, 需要扩大
            buffer_.resize(write_pos_ + size + 1);
        } else {
            size_t readable = readableBytes();
            // 放的下, 把readable的数据移至开头
            std::copy(begin() + read_pos_, begin() + write_pos_, begin());
            read_pos_ = 0;
            write_pos_ = read_pos_ + readable;
            assert(readable == readableBytes());
        }
    }
    // 向buffer写入数据后改变写指针
    void hasWritten(size_t len) {
        write_pos_ += len;
    }
    std::vector<char> buffer_;
    size_t read_pos_;
    size_t write_pos_;
    // 为什么要使用atomic包装pos?
    // 如果是为了线程安全, 那么应该出现两个以上的线程操作同一个buffer实例的情况
};



#endif //BUFFER_H
