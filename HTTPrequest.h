// encode UTF-8

// @Author        : JenkinsY
// @Date          : 2022-03-28

#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <regex>

#include "buffer.h"

using namespace std;

class HTTPrequest
{
public:
    enum PARSE_STATE{
        REQUEST_LINE,
        HEADERS,
        BODY,
        FINISH,
    };

    enum HTTP_CODE {
        NO_REQUEST = 0,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURSE,
        FORBIDDENT_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION,
    };

    HTTPrequest() {init();};
    ~HTTPrequest()=default;

    void init();
    bool parse(Buffer& buff); //解析HTTP请求

    //获取HTTP信息
    string path() const;
    string& path();
    string method() const;
    string version() const;
    string getPost(const string& key) const;
    string getPost(const char* key) const;

    bool isKeepAlive() const;

private:
    bool parseRequestLine_(const string& line);//解析请求行
    void parseRequestHeader_(const string& line); //解析请求头
    void parseDataBody_(const string& line); //解析数据体


    void parsePath_();
    void parsePost_();

    static int convertHex(char ch);

    PARSE_STATE state_;
    string method_,path_,version_,body_;
    unordered_map<string, string>header_;
    unordered_map<string, string>post_;

    static const unordered_set<string>DEFAULT_HTML;
   


};





#endif  //HTTP_REQUEST_H
