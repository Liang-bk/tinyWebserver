//
// Created by 86183 on 2025/3/25.
//

#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H
#pragma once

#include <mysql/mysql.h>

#include <regex>
#include <unordered_set>

#include "buffer/buffer.h"
#include "logger/logger.h"
#include "pool/sqlconnRAll.h"

class HttpRequest {
public:
    enum PARSE_STATE {
        REQUEST_LINE, // 正在解析请求行
        HEADERS, // 请求头
        BODY, // 请求体
        FINISH, // 解析完成
    };

    enum HTTP_CODE {
        NO_REQUEST = 0,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURSE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION,
    };

    HttpRequest();

    ~HttpRequest() = default;

    void init();

    bool parse(Buffer &buffer);

    std::string path() const;

    std::string &path();

    std::string method() const;

    std::string version() const;

    std::string getPost(const std::string &key) const;

    std::string getPost(const char *key) const;

    bool isKeepAlive() const;

private:
    bool parseRequestLine(const std::string &line);

    void parseHeaders(const std::string &line);

    void parseBody(const std::string &line);

    void parsePath();

    void parsePost();

    void parseFromUrlEncoded();

    static bool userVerify(const std::string &name, const std::string &pwd,
                           bool is_login);

    PARSE_STATE state_; // 当前解析的状态
    std::string method_; // 请求方法
    std::string path_; // 请求路径
    std::string version_; // 协议版本
    std::string body_; // 请求体
    std::unordered_map<std::string, std::string> headers_; // 请求头信息
    std::unordered_map<std::string, std::string> post_; // post表单信息
    static const std::unordered_set<std::string> DEFAULT_HTML; // 默认的网页
    static const std::unordered_map<std::string, int> DEFAULT_HTML_TAG;

    static int hexToDec(char ch); // 十六进制转十进制
};


#endif //HTTP_REQUEST_H
