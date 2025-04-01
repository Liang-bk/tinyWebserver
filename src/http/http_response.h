//
// Created by 86183 on 2025/3/30.
//

#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H
#include "buffer/buffer.h"
#include "logger/logger.h"
#include <fcntl.h>  // open
#include <unistd.h> // close
#include <unordered_map>
#include <sys/stat.h>   // stat
#include <sys/mman.h>   // mmap, munmap


class HttpResponse {
public:
    HttpResponse();

    ~HttpResponse();

    void init(const std::string &src_dir, std::string &path,
              bool is_keep_alve = false, int status_code = -1);

    void makeResponse(Buffer &buffer);

    void unmapFile();

    char *getFile();

    size_t getFileSize() const;

    void errorContent(Buffer &buffer, std::string msg);

    int getStatusCode() const { return status_code_; }

private:
    void addStateLine(Buffer &buffer);

    void addHeader(Buffer &buffer);

    void addContent(Buffer &buffer);

    void errorHtml();

    std::string getFileType();

    int status_code_; // 状态码
    bool is_keep_alive_; // 长连接
    std::string path_; // 资源路径
    std::string src_dir_; // 资源目录

    char *mm_file_; // 文件内存映射指针
    struct stat mm_file_stat_; // 文件状态信息

    static const std::unordered_map<std::string, std::string> SUFFIX_TYPE; // 后缀类型
    static const std::unordered_map<int, std::string> CODE_STATUS; // 状态码-描述
    static const std::unordered_map<int, std::string> CODE_PATH; // 状态码-路径
};


#endif //HTTP_RESPONSE_H
