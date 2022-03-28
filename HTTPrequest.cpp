// encode UTF-8

// @Author        : JenkinsY
// @Date          : 2022-03-28

#include "HTTPrequest.h"
using namespace std;

const unordered_set<string> HTTPrequest::DEFAULT_HTML{
            "/index", "/welcome", "/video", "/picture"};

void HTTPrequest::init() {
    method_ = path_ = version_ = body_ = "";
    state_ = REQUEST_LINE;
    header_.clear();
    post_.clear();
}

bool HTTPrequest::isKeepAlive() const {
    if(header_.count("Connection") == 1) {
        return header_.find("Connection")->second == "keep-alive" && version_ == "1.1";
    }
    return false;
}

bool HTTPrequest::parse(Buffer& buff) {
    const char CRLF[] = "\r\n";
    if(buff.readableBytes() <= 0) {
        return false;
    }
    //cout<<"parse buff start:"<<endl;
    //buff.printContent();
    //cout<<"parse buff finish:"<<endl;
    while(buff.readableBytes() && state_ != FINISH) {
        const char* lineEnd = search(buff.curReadPtr(), buff.curWritePtrConst(), CRLF, CRLF + 2);
        string line(buff.curReadPtr(), lineEnd);
        switch(state_)
        {
        case REQUEST_LINE:
            //cout<<"REQUEST: "<<line<<endl;
            if(!parseRequestLine_(line)) {
                return false;
            }
            parsePath_();
            break;
        case HEADERS:
            parseRequestHeader_(line);
            if(buff.readableBytes() <= 2) {
                state_ = FINISH;
            }
            break;
        case BODY:
            parseDataBody_(line);
            break;
        default:
            break;
        }
        if(lineEnd == buff.curWritePtr())  break;
        buff.updateReadPtrUntilEnd(lineEnd + 2);
    }
    return true;
}

/* 解析地址 */
void HTTPrequest::parsePath_() {
    if(path_ == "/") {
        path_ = "/index.html"; 
    }
    else {
        for(auto &item: DEFAULT_HTML) {
            if(item == path_) {
                path_ += ".html";
                break;
            }
        }
    }
}


/* 解析请求行 */
bool HTTPrequest::parseRequestLine_(const string& line) {
    regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    smatch subMatch;
    if(regex_match(line, subMatch, patten)) {   
        method_ = subMatch[1];
        path_ = subMatch[2];
        version_ = subMatch[3];
        state_ = HEADERS;
        return true;
    }
    return false;
}

/* 解析请求头 */
void HTTPrequest::parseRequestHeader_(const string& line) {
    regex patten("^([^:]*): ?(.*)$");
    smatch subMatch;
    if(regex_match(line, subMatch, patten)) {
        header_[subMatch[1]] = subMatch[2];
    }
    else {
        state_ = BODY;
    }
}

/* 解析请求消息体，根据消息类型解析内容 */
void HTTPrequest::parseDataBody_(const string& line) {
    body_ = line;
    parsePost_();
    state_ = FINISH;
}

int HTTPrequest::convertHex(char ch) {
    if(ch >= 'A' && ch <= 'F') return ch -'A' + 10;
    if(ch >= 'a' && ch <= 'f') return ch -'a' + 10;
    return ch;
}

void HTTPrequest::parsePost_() {
    if(method_ == "POST" && header_["Content-Type"] == "application/x-www-form-urlencoded") {
        if(body_.size() == 0) { return; }

        string key, value;
        int num = 0;
        int n = body_.size();
        int i = 0, j = 0;

        for(; i < n; i++) {
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
                num = convertHex(body_[i + 1]) * 16 + convertHex(body_[i + 2]);
                body_[i + 2] = num % 10 + '0';
                body_[i + 1] = num / 10 + '0';
                i += 2;
                break;
            case '&':
                value = body_.substr(j, i - j);
                j = i + 1;
                post_[key] = value;
                break;
            default:
                break;
            }
        }
        assert(j <= i);
        if(post_.count(key) == 0 && j < i) {
            value = body_.substr(j, i - j);
            post_[key] = value;
        }
    }
}

string HTTPrequest::path() const{
    return path_;
}

string& HTTPrequest::path(){
    return path_;
}
string HTTPrequest::method() const {
    return method_;
}

string HTTPrequest::version() const {
    return version_;
}

string HTTPrequest::getPost(const string& key) const {
    assert(key != "");
    if(post_.count(key) == 1) {
        return post_.find(key)->second;
    }
    return "";
}

string HTTPrequest::getPost(const char* key) const {
    assert(key != nullptr);
    if(post_.count(key) == 1) {
        return post_.find(key)->second;
    }
    return "";
}
