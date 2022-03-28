// encode UTF-8

// @Author        : JenkinsY
// @Date          : 2022-03-28

#include "HTTPconnection.h"

using namespace std;

const char* HTTPconnection::srcDir;
atomic<int> HTTPconnection::userCount;
bool HTTPconnection::isET;

HTTPconnection::HTTPconnection() { 
    fd_ = -1;
    addr_ = { 0 };
    isClose_ = true;
};

HTTPconnection::~HTTPconnection() { 
    closeHTTPConn();
};

/* 连接初始化：套接字，端口，缓存，请求解析状态机 */
void HTTPconnection::initHTTPConn(int fd, const sockaddr_in& addr) {
    assert(fd > 0);
    userCount++;
    addr_ = addr;
    fd_ = fd;
    writeBuffer_.initPtr();
    readBuffer_.initPtr();
    isClose_ = false;
}

/* 关闭连接 */
void HTTPconnection::closeHTTPConn() {
    response_.unmapFile_();
    if(isClose_ == false){
        isClose_ = true; 
        userCount--;
        close(fd_);
    }
}

int HTTPconnection::getFd() const {
    return fd_;
};

struct sockaddr_in HTTPconnection::getAddr() const {
    return addr_;
}

const char* HTTPconnection::getIP() const {
    return inet_ntoa(addr_.sin_addr);
}

int HTTPconnection::getPort() const {
    return addr_.sin_port;
}


/* 读方法，ET模式会将缓存读空 */
//返回最后一次读取的长度，以及错误类型
ssize_t HTTPconnection::readBuffer(int* saveErrno) {
    //最后一次读取的长度
    ssize_t len = -1;
    do {
        len = readBuffer_.readFd(fd_, saveErrno);
        //cout<<fd_<<" read bytes:"<<len<<endl;
        if (len <= 0) {
            break;
        }
    } while (isET);
    return len;
}

/* 写方法,响应头和响应体是分开的，要用iov实现写操作 */
ssize_t HTTPconnection::writeBuffer(int* saveErrno) {
    //最后一次写入的长度
    ssize_t len = -1;
    do {
        len = writev(fd_, iov_, iovCnt_);
        if(len <= 0) {
            *saveErrno = errno;
            break;
        }
        //缓存为空，传输完成
        if(iov_[0].iov_len + iov_[1].iov_len  == 0) break;
        //响应头已经传输完成
        else if(static_cast<size_t>(len) > iov_[0].iov_len) {
            //更新响应体传输起点和长度
            iov_[1].iov_base = (uint8_t*) iov_[1].iov_base + (len - iov_[0].iov_len);
            iov_[1].iov_len -= (len - iov_[0].iov_len);
            //响应头不再需要传输
            if(iov_[0].iov_len) {
                //响应头保存在写缓存中，全部回收即可
                writeBuffer_.initPtr();
                iov_[0].iov_len = 0;
            }
        }
        //响应头还没传输完成
        else {
            //更新响应头传输起点和长度
            iov_[0].iov_base = (uint8_t*)iov_[0].iov_base + len; 
            iov_[0].iov_len -= len; 
            writeBuffer_.updateReadPtr(len);
        }
    } while(isET || writeBytes() > 10240);  //一次最多传输10MB数据
    return len;
}

/* 处理方法：解析读缓存内的请求报文，判断是否完整 */
//不完整返回false，完整在写缓存内写入响应头，并获取响应体内容（文件）
bool HTTPconnection::handleHTTPConn() {
    request_.init();
    //请求不完整，继续读
    if(readBuffer_.readableBytes() <= 0) {
        //cout<<"readBuffer is empty!"<<endl;
        return false;
    }
    //请求完整，开始写
    else if(request_.parse(readBuffer_)) {
        response_.init(srcDir, request_.path(), request_.isKeepAlive(), 200);
    }
    //请求行错误, bad request
    else {
        cout<<"400!"<<endl;
        //readBuffer_.printContent();
        response_.init(srcDir, request_.path(), false, 400);
    }

    response_.makeResponse(writeBuffer_);
    /* 响应头 */
    iov_[0].iov_base = const_cast<char*>(writeBuffer_.curReadPtr());
    iov_[0].iov_len = writeBuffer_.readableBytes();
    iovCnt_ = 1;

    /* 响应体 */
    /* 文件 */
    if(response_.fileLen() > 0  && response_.file()) {
        iov_[1].iov_base = response_.file();
        iov_[1].iov_len = response_.fileLen();
        iovCnt_ = 2;
    }
    return true;
}
