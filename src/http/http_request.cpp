//
// Created by 86183 on 2025/3/25.
//

#include "http_request.h"

const std::unordered_set<std::string> HttpRequest::DEFAULT_HTML{
    "/index", "/register", "/login",
    "/welcome", "/video", "/picture",
};
const std::unordered_map<std::string, int> HttpRequest::DEFAULT_HTML_TAG{
    {"/register.html", 0}, {"/login.html", 1},
};

HttpRequest::HttpRequest() {
    init();
}

void HttpRequest::init() {
    method_ = path_ = version_ = body_ = "";
    state_ = REQUEST_LINE;
    headers_.clear();
    post_.clear();
}

bool HttpRequest::isKeepAlive() const {
    if (headers_.count("Connection") == 1) {
        return headers_.find("Connection")->second == "keep-alive" && version_ == "1.1";
    }
    return false;
}

bool HttpRequest::parse(Buffer &buffer) {
    const char CRLF[] = "\r\n";
    if (buffer.readableBytes() <= 0) {
        return false;
    }

    while (buffer.readableBytes() && state_ != FINISH) {
        // search(): 从[first1, last1) 这一段内找到 [first2, last2), 返回找到的first2的位置
        const char *line_end = std::search(buffer.peek(), buffer.beginWriteConst(), CRLF, CRLF + 2);
        // 预读取
        std::string line(buffer.peek(), line_end);
        switch (state_) {
            // request_line和headers比较特殊, 读取完整的一行再处理, 防止出错
            case REQUEST_LINE:
                // if (line_end == buffer.beginWriteConst()) {
                //     return false;
                // }
                if (!parseRequestLine(line)) {
                    return false;
                }
                parsePath();
                break;
            case HEADERS:
                // if (line_end == buffer.beginWriteConst()) {
                //     return false;
                // }
                parseHeaders(line);
            // 下面这个判断私以为没什么用
                if (buffer.readableBytes() <= 2) {
                    state_ = FINISH;
                    break;
                }
                break;
            case BODY:
                // body不带CRLF, 不清楚会不会有bug, 暂时先这么写吧
                if (line_end != buffer.beginWriteConst() && line.empty()) {
                    state_ = FINISH;
                    break;
                }
                parseBody(line);
                break;
            default:
                break;
        }
        if (line_end == buffer.beginWriteConst()) {
            break;
        }
        // 跳过CRLF
        buffer.retrieveUntil(line_end + 2);
    }
    // 请求方法, 请求路径, http版本
    LOG_DEBUG("[%s], [%s], [%s]", method_.c_str(), path_.c_str(), version_.c_str());
    return true;
}

void HttpRequest::parsePath() {
    // /index.html
    // 根目录
    if (path_ == "/") {
        path_ = "/index.html";
    } else {
        // 其他页面
        auto pathExist = [this]() -> bool {
            for (auto page: DEFAULT_HTML) {
                if (path_ == page) {
                    return true;
                }
            }
            return false;
        };
        if (pathExist()) {
            path_ += ".html";
        } else {
            path_ = "/index.html";
        }
    }
}

bool HttpRequest::parseRequestLine(const std::string &line) {
    // GET /index.html HTTP/1.1
    // 开头的^abc表示匹配以abc开头的行, abc$表示匹配以abc结尾的行, 则^abc$表示只匹配abc
    // ()的作用是捕获分组, 不然regex_match只能返回true or false
    // [^ ]*表示只要不是空格就能匹配上, *表示前面的匹配能匹配0次或多次
    std::regex pattern("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    std::smatch match_results;
    if (std::regex_match(line, match_results, pattern)) {
        method_ = match_results[1].str();
        path_ = match_results[2].str();
        version_ = match_results[3].str();
        state_ = HEADERS;
        return true;
    }
    LOG_ERROR("Request Line parse error");
    return false;
}

void HttpRequest::parseHeaders(const std::string &line) {
    // key: value
    // ([^:]+) 匹配非:的字符一个或多个
    // : ? 匹配:和0-1个' '
    // (.*) 匹配任意数量字符
    std::regex pattern("^([^:]+): ?(.*)$");
    std::smatch match_results;
    if (std::regex_match(line, match_results, pattern)) {
        headers_[match_results[1].str()] = match_results[2].str();
    } else {
        // 请求头只剩下CRLF, 下一步应该解析BODY
        state_ = BODY;
    }
}

void HttpRequest::parseBody(const std::string &line) {
    body_ = line;
    parsePost();
    state_ = FINISH;
    LOG_DEBUG("Body:%s, len:%d", line.c_str(), line.size());
}

void HttpRequest::parsePost() {
    if (method_ == "POST" && headers_["Content-Type"] == "application/x-www-form-urlencoded") {
        // 解析表单信息
        parseFromUrlEncoded();
        if (DEFAULT_HTML_TAG.count(path_)) {
            int tag = DEFAULT_HTML_TAG.find(path_)->second;
            LOG_DEBUG("Tag:%d", tag);
            if (tag == 0 || tag == 1) {
                bool is_login = (tag == 1);
                if (userVerify(post_["username"], post_["password"], is_login)) {
                    path_ = "/welcome.html";
                } else {
                    path_ = "/error.html";
                }
            }
        }
    }
}

void HttpRequest::parseFromUrlEncoded() {
    if (body_.size() == 0) {
        return;
    }
    // username=zhangsan&password=123
    std::string key, value;
    int num = 0;
    int n = body_.size();
    int i = 0, j = 0;
    for (; i < n; ++i) {
        char ch = body_[i];
        switch (ch) {
            case '=':
                key = body_.substr(j, i - j);
                j = i + 1;
                break;
            case '+':
                body_[i] = ' ';
                break;
            case '%':
                // 加密, 编码
                num = hexToDec(body_[i + 1]) * 16 + hexToDec(body_[i + 2]);
                body_[i + 2] = num % 10 + '0';
                body_[i + 1] = num / 10 + '0';
                i += 2;
                break;
            case '&':
                value = body_.substr(j, i - j);
                j = i + 1;
                post_[key] = value;
                LOG_DEBUG("%s = %s", key.c_str(), value.c_str());
                break;
            default:
                break;
        }
    }
    assert(j <= i);
    if (post_.count(key) == 0 && j < i) {
        value = body_.substr(j, i - j);
        post_[key] = value;
    }
}

/// 验证注册或者登录
/// @param name 用户名
/// @param pwd 密码
/// @param is_login 判断是注册还是登录请求
/// @return 验证成功或失败
bool HttpRequest::userVerify(const std::string &name, const std::string &pwd, bool is_login) {
    if (name == "" || pwd == "") {
        return false;
    }
    LOG_INFO("Verify name:%s, pwd:%s", name.c_str(), pwd.c_str());
    MYSQL *sql = nullptr;
    // SQLConnPool在其他地方初始化
    SQLConnRAll sql_conn_rall(&sql, SQLConnPool::getInstance());
    assert(sql);

    bool flag = false;
    unsigned int j = 0;
    char order[256] = {};
    // 表的列
    MYSQL_FIELD *field = nullptr;
    MYSQL_RES *res = nullptr;

    if (!is_login) {
        flag = true;
    }
    snprintf(order, 256, "SELECT username, password FROM user WHERE username='%s' LIMIT 1", name.c_str());
    LOG_DEBUG("order: %s", order);
    // mysql_query查询成功时返回0
    if (mysql_query(sql, order)) {
        mysql_free_result(res);
        return false;
    }

    res = mysql_store_result(sql);
    // 有多少列, 这里应该是2列
    j = mysql_num_fields(res);
    // 提取列
    field = mysql_fetch_field(res);
    // 遍历行
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        LOG_DEBUG("MYSQL ROW: %s %s", row[0], row[1]);
        std::string password(row[1]);
        // 登录请求
        if (is_login) {
            if (pwd == password) {
                flag = true;
            } else {
                flag = false;
                LOG_DEBUG("pwd error!");
            }
        } else {
            // 需要注册但已有相同的用户名
            flag = false;
            LOG_DEBUG("user used!");
        }
    }
    mysql_free_result(res);
    // 尝试注册
    if (!is_login && flag == true) {
        LOG_DEBUG("register!");
        bzero(order, 256);
        snprintf(order, 256, "INSERT INTO user(username, password) VALUES('%s', '%s')", name.c_str(), pwd.c_str());
        LOG_DEBUG("order: %s", order);
        if (mysql_query(sql, order)) {
            LOG_DEBUG("Insert error!");
            flag = false;
        }
    }
    LOG_DEBUG("user verify success!");
    return flag;
}

// 十六进制转十进制
int HttpRequest::hexToDec(char ch) {
    if (isdigit(ch)) {
        return ch - '0';
    } else if (isalpha(ch)) {
        ch = tolower(ch);
        return ch - 'a' + 10;
    } else {
        return -1;
    }
}

std::string &HttpRequest::path() {
    return path_;
}

std::string HttpRequest::path() const {
    return path_;
}

std::string HttpRequest::version() const {
    return version_;
}

std::string HttpRequest::method() const {
    return method_;
}

std::string HttpRequest::getPost(const char *key) const {
    assert(key != nullptr);
    if (post_.count(key) == 1) {
        return post_.find(key)->second;
    }
    return "";
}

std::string HttpRequest::getPost(const std::string &key) const {
    assert(key != "");
    if (post_.count(key) == 1) {
        return post_.find(key)->second;
    }
    return "";
}
