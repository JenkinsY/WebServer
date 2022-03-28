// encode UTF-8

// @Author        : JenkinsY
// @Date          : 2022-03-28

#include "HTTPresponse.h"

using namespace std;

const unordered_map<string, string> HTTPresponse::SUFFIX_TYPE = {
    { ".html",  "text/html" },
    { ".xml",   "text/xml" },
    { ".xhtml", "application/xhtml+xml" },
    { ".txt",   "text/plain" },
    { ".rtf",   "application/rtf" },
    { ".pdf",   "application/pdf" },
    { ".word",  "application/nsword" },
    { ".png",   "image/png" },
    { ".gif",   "image/gif" },
    { ".jpg",   "image/jpeg" },
    { ".jpeg",  "image/jpeg" },
    { ".au",    "audio/basic" },
    { ".mpeg",  "video/mpeg" },
    { ".mpg",   "video/mpeg" },
    { ".avi",   "video/x-msvideo" },
    { ".gz",    "application/x-gzip" },
    { ".tar",   "application/x-tar" },
    { ".css",   "text/css "},
    { ".js",    "text/javascript "},
};

const unordered_map<int, string> HTTPresponse::CODE_STATUS = {
    { 200, "OK" },
    { 400, "Bad Request" },
    { 403, "Forbidden" },
    { 404, "Not Found" },
};

const unordered_map<int, string> HTTPresponse::CODE_PATH = {
    { 400, "/400.html" },
    { 403, "/403.html" },
    { 404, "/404.html" },
};

HTTPresponse::HTTPresponse() {
    code_ = -1;
    path_ = srcDir_ = "";
    isKeepAlive_ = false;
    mmFile_ = nullptr; 
    mmFileStat_ = { 0 };
};

HTTPresponse::~HTTPresponse() {
    unmapFile_();
}

/* 响应报文初始化： 状态码，长连接，文件路径 */
void HTTPresponse::init(const string& srcDir, string& path, bool isKeepAlive, int code){
    assert(srcDir != "");
    if(mmFile_) { unmapFile_(); }
    code_ = code;
    isKeepAlive_ = isKeepAlive;
    path_ = path;
    srcDir_ = srcDir;
    mmFile_ = nullptr; 
    mmFileStat_ = { 0 };
}

/* 制作响应报文 */
void HTTPresponse::makeResponse(Buffer& buff) {
    /* 判断请求的资源文件 */
    if(stat((srcDir_ + path_).data(), &mmFileStat_) < 0 || S_ISDIR(mmFileStat_.st_mode)) {
        code_ = 404;
    }
    else if(!(mmFileStat_.st_mode & S_IROTH)) {
        code_ = 403;
    }
    else if(code_ == -1) { 
        code_ = 200; 
    }
    //如果代码存在，跳转到相应页面，否则跳转到错误页面
    errorHTML_();
    //添加状态行
    addStateLine_(buff);
    //添加响应头
    addResponseHeader_(buff);
    //添加响应体
    addResponseContent_(buff);
}

/* 获取映射好的文件 */
char* HTTPresponse::file() {
    return mmFile_;
}

size_t HTTPresponse::fileLen() const {
    return mmFileStat_.st_size;
}

/* 范围内的错误页面 */
void HTTPresponse::errorHTML_() {
    if(CODE_PATH.count(code_) == 1) {
        path_ = CODE_PATH.find(code_)->second;
        stat((srcDir_ + path_).data(), &mmFileStat_);
    }
}

/* 添加状态行 */
void HTTPresponse::addStateLine_(Buffer& buff) {
    string status;
    if(CODE_STATUS.count(code_) == 1) {
        status = CODE_STATUS.find(code_)->second;
    }
    else {
        code_ = 400;
        status = CODE_STATUS.find(400)->second;
    }
    buff.append("HTTP/1.1 " + to_string(code_) + " " + status + "\r\n");
}

//添加响应头
void HTTPresponse::addResponseHeader_(Buffer& buff) {
    buff.append("Connection: ");
    if(isKeepAlive_) {
        buff.append("keep-alive\r\n");
        buff.append("keep-alive: max=6, timeout=120\r\n");
    } else{
        buff.append("close\r\n");
    }
    buff.append("Content-type: " + getFileType_() + "\r\n");
}

/* 添加响应体 */
void HTTPresponse::addResponseContent_(Buffer& buff) {
    int srcFd = open((srcDir_ + path_).data(), O_RDONLY);
    if(srcFd < 0) { 
        errorContent(buff, "File NotFound!");
        return; 
    }

    // 将文件映射到内存提高文件的访问速度 
    // MAP_PRIVATE 建立一个写入时拷贝的私有映射
    int* mmRet = (int*)mmap(0, mmFileStat_.st_size, PROT_READ, MAP_PRIVATE, srcFd, 0);
    if(*mmRet == -1) {
        errorContent(buff, "File NotFound!");
        return; 
    }
    mmFile_ = (char*)mmRet;
    close(srcFd);
    buff.append("Content-length: " + to_string(mmFileStat_.st_size) + "\r\n\r\n");
}

/* 解除文件映射 */
void HTTPresponse::unmapFile_() {
    if(mmFile_) {
        munmap(mmFile_, mmFileStat_.st_size);
        mmFile_ = nullptr;
    }
}

/* 判断文件类型 */
string HTTPresponse::getFileType_() {
    string::size_type idx = path_.find_last_of('.');
    if(idx == string::npos) {
        return "text/plain";
    }
    string suffix = path_.substr(idx);
    if(SUFFIX_TYPE.count(suffix) == 1) {
        return SUFFIX_TYPE.find(suffix)->second;
    }
    return "text/plain";
}

/* 范围外错误页面 */
void HTTPresponse::errorContent(Buffer& buff, string message) 
{
    string body;
    string status;
    body += "<html><title>Error</title>";
    body += "<body bgcolor=\"ffffff\">";
    if(CODE_STATUS.count(code_) == 1) {
        status = CODE_STATUS.find(code_)->second;
    } else {
        status = "Bad Request";
    }
    body += to_string(code_) + " : " + status  + "\n";
    body += "<p>" + message + "</p>";
    body += "<hr><em>TinyWebServer</em></body></html>";

    buff.append("Content-length: " + to_string(body.size()) + "\r\n\r\n");
    buff.append(body);
}
