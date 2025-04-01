//
// Created by 86183 on 2025/3/30.
//

#include "http_response.h"

#include <bits/fs_path.h>
/* 响应结构:
 * 状态行: HTTP/1.1 200 OK (版本 状态码 状态消息)
 * 响应头: Content-Type: text/html
 *         Content-Length: 1234
 * 响应体: <html>...</html>
 */
// 文件后缀对应的TYPE类型, 用在响应头Content-Type中
const std::unordered_map<std::string, std::string> HttpResponse::SUFFIX_TYPE{
    {".html", "text/html"},
    {".xml", "text/xml"},
    {".xhtml", "application/xhtml+xml"},
    {".txt", "text/plain"},
    {".rtf", "application/rtf"},
    {".pdf", "application/pdf"},
    {".word", "application/nsword"},
    {".png", "image/png"},
    {".gif", "image/gif"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".au", "audio/basic"},
    {".mpeg", "video/mpeg"},
    {".mpg", "video/mpeg"},
    {".avi", "video/x-msvideo"},
    {".gz", "application/x-gzip"},
    {".tar", "application/x-tar"},
    {".css", "text/css "},
    {".js", "text/javascript "},
};

// 响应状态码对应的描述
const std::unordered_map<int, std::string> HttpResponse::CODE_STATUS{
    {200, "OK"},
    {400, "Bad Request"},
    {403, "Forbidden"},
    {404, "Not Found"},
};

// 错误响应码对应的资源路径
const std::unordered_map<int, std::string> HttpResponse::CODE_PATH{
    {400, "/400.html"},
    {403, "/403.html"},
    {404, "/404.html"},
};

HttpResponse::HttpResponse() {
    status_code_ = -1;
    path_ = src_dir_ = "";
    is_keep_alive_ = false;
    mm_file_ = nullptr;
    mm_file_stat_ = {};
}

HttpResponse::~HttpResponse() {
    unmapFile();
}

void HttpResponse::init(const std::string &src_dir, std::string &path, bool is_keep_alve, int status_code) {
    assert(src_dir != "");

    if (mm_file_) {
        unmapFile();
    }
    status_code_ = status_code;
    is_keep_alive_ = is_keep_alve;
    path_ = path;
    src_dir_ = src_dir;
    mm_file_ = nullptr;
    mm_file_stat_ = {};
}

void HttpResponse::makeResponse(Buffer &buffer) {
    // index.html -> /home/user/code/resources/index.html
    // dir + path: 拼接后的资源路径
    // 获取资源路径失败或者资源路径是一个目录, 返回404
    if (stat((src_dir_ + path_).data(), &mm_file_stat_) < 0 || S_ISDIR(mm_file_stat_.st_mode)) {
        status_code_ = 404;
    } else if (!(mm_file_stat_.st_mode & S_IROTH)) {
        // 文件权限, 可被other读返回1, 否则返回0
        // 如果文件不允许other读, 403
        status_code_ = 403;
    } else if (status_code_ == -1) {
        status_code_ = 200;
    }
    errorHtml();
    addStateLine(buffer);
    addHeader(buffer);
    addContent(buffer);
}

char *HttpResponse::getFile() {
    return mm_file_;
}

size_t HttpResponse::getFileSize() const {
    return mm_file_stat_.st_size;
}

void HttpResponse::errorHtml() {
    if (CODE_PATH.count(status_code_) == 1) {
        path_ = CODE_PATH.find(status_code_)->second;
        stat((src_dir_ + path_).data(), &mm_file_stat_);
    }
}

// 响应行
void HttpResponse::addStateLine(Buffer &buffer) {
    std::string status;
    if (CODE_STATUS.count(status_code_) == 1) {
        status = CODE_STATUS.find(status_code_)->second;
    } else {
        status_code_ = 400;
        status = CODE_STATUS.find(status_code_)->second;
    }
    buffer.append("HTTP/1.1 " + std::to_string(status_code_) + " " + status + "\r\n");
}

// 添加响应头
void HttpResponse::addHeader(Buffer &buffer) {
    buffer.append("Connection: ");
    if (is_keep_alive_) {
        buffer.append("keep-alive\r\n");
        buffer.append("keep-alive: max=6, timeout=120\r\n");
    } else {
        buffer.append("close\r\n");
    }
    buffer.append("Content-Type: " + getFileType() + "\r\n");
    // buffer.append("Content-Length: " + std::to_string(getFileSize()) + "\r\n\r\n");
}

// 添加响应内容
void HttpResponse::addContent(Buffer &buffer) {
    int file_fd = open((src_dir_ + path_).data(), O_RDONLY);
    if (file_fd < 0) {
        errorContent(buffer, "File Not Found!");
        return;
    }

    /* 文件映射
     *
     */
    LOG_DEBUG("file path: %s", (src_dir_ + path_).data());
    int *mm_ret = (int *) mmap(0, mm_file_stat_.st_size, PROT_READ, MAP_PRIVATE, file_fd, 0);
    if (*mm_ret == -1) {
        errorContent(buffer, "File Not Found!");
        return;
    }
    mm_file_ = (char *) mm_ret;
    close(file_fd);
    buffer.append("Content-Length: " + std::to_string(mm_file_stat_.st_size) + "\r\n\r\n");
}

void HttpResponse::unmapFile() {
    if (mm_file_) {
        munmap(mm_file_, mm_file_stat_.st_size);
        mm_file_ = nullptr;
    }
}

std::string HttpResponse::getFileType() {
    // 判断文件类型
    std::string::size_type idx = path_.find_last_of('.');
    if (idx == std::string::npos) {
        return "text/plain";
    }
    std::string suffix = path_.substr(idx);
    if (SUFFIX_TYPE.count(suffix) == 1) {
        return SUFFIX_TYPE.find(suffix)->second;
    }
    return "text/plain";
}

// 获取内容失败时, 指向错误页面
void HttpResponse::errorContent(Buffer &buffer, std::string msg) {
    std::string body;
    std::string status;
    body += "<html><title>Error</title>";
    body += "<body bgcolor=\"ffffff\">";
    if (CODE_STATUS.count(status_code_) == 1) {
        status = CODE_STATUS.find(status_code_)->second;
    } else {
        status = "Bad Request";
    }
    body += std::to_string(status_code_) + " : " + status + "\n";
    body += "<p>" + msg + "</p>";
    body += "<hr><em>TinyWebServer</em></body></html>";

    buffer.append("Content-Length: " + std::to_string(body.size()) + "\r\n\r\n");
    buffer.append(body);
}
