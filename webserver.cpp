// encode UTF-8

// @Author        : JenkinsY
// @Date          : 2022-3-28

#include "webserver.h"

WebServer::WebServer(
    int port,int trigMode,int timeoutMS,bool optLinger,int threadNum):
    port_(port),openLinger_(optLinger),timeoutMS_(timeoutMS),isClose_(false),
    timer_(new TimerManager()),threadpool_(new ThreadPool(threadNum)),epoller_(new Epoller())
{
    //获取当前工作目录的绝对路径
    srcDir_=getcwd(nullptr,256);
    assert(srcDir_);
    //拼接字符串
    strncat(srcDir_,"/resources/",16);
    HTTPconnection::userCount=0;
    HTTPconnection::srcDir=srcDir_;

    //设置不同套接字的触发模式
    initEventMode_(trigMode);
    if(!initSocket_()) isClose_=true;

}

WebServer::~WebServer()
{
    //回收监听套接字
    close(listenFd_);
    isClose_=true;
    //回收路径动态缓存
    free(srcDir_);
}

/* 设置不同套接字的触发模式 */
void WebServer::initEventMode_(int trigMode) {
    //监听事件：仅为了初始化，无作用
    listenEvent_ = EPOLLRDHUP;
    //连接事件：对端断开，并设置oneshot
    connectionEvent_ = EPOLLONESHOT | EPOLLRDHUP;
    switch (trigMode)
    {
    case 0:
        break;
    case 1:
        connectionEvent_ |= EPOLLET;
        break;
    case 2:
        listenEvent_ |= EPOLLET;
        break;
    case 3:
        listenEvent_ |= EPOLLET;
        connectionEvent_ |= EPOLLET;
        break;
    default:
        listenEvent_ |= EPOLLET;
        connectionEvent_ |= EPOLLET;
        break;
    }
    //用&判断连接套接字是否为ET触发模式
    HTTPconnection::isET = (connectionEvent_ & EPOLLET);
}


/* epoll循环监听事件，根据事件类型调用相应方法 */
//延迟计算思想，方法及参数会被打包放到线程池的任务队列中
void WebServer::Start()
{
    int timeMS=-1;//epoll wait timeout==-1就是无事件一直阻塞
    if(!isClose_)
    {
        std::cout<<"============================";
        std::cout<<"Server Start!";
        std::cout<<"============================";
        std::cout<<std::endl;
    }
    while(!isClose_)
    {
        //返回下一个计时器超时的时间
        if(timeoutMS_>0)
        {
            timeMS=timer_->getNextHandle();
        }
        /* 利用epoll的time_wait实现定时功能 */
        //在计时器超时前唤醒一次epoll，判断是否有新事件到达
        //如果没有新事件，下次调用getNextTick时，会将超时的堆顶计时器删除
        int eventCnt=epoller_->wait(timeMS);
        //遍历事件表
        for(int i=0;i<eventCnt;++i)
        {
            //事件套接字
            int fd=epoller_->getEventFd(i);
            //事件内容
            uint32_t events=epoller_->getEvents(i);
            //监听套接字只有连接事件
            if(fd==listenFd_)
            {
                handleListen_();
                //std::cout<<fd<<" is listening!"<<std::endl;
            }
            //连接套接字几种事件

            //对端关闭连接
            else if(events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                assert(users_.count(fd) > 0);
                closeConn_(&users_[fd]);
            }
            //读事件
            else if(events & EPOLLIN) {
                assert(users_.count(fd) > 0);
                handleRead_(&users_[fd]);
                //std::cout<<fd<<" reading end!"<<std::endl;
            }
            //写事件
            else if(events & EPOLLOUT) {
                assert(users_.count(fd) > 0);
                handleWrite_(&users_[fd]);
            }
            else {
                std::cout<<"Unexpected event"<<std::endl;
            }
        }
    }
}

/* 发送错误 */
void WebServer::sendError_(int fd, const char* info)
{
    assert(fd>0);
    int ret=send(fd,info,strlen(info),0);
    if(ret<0)
    {
        //std::cout<<"send error to client"<<fd<<" error!"<<std::endl;
    }
    close(fd);
}


/* 关闭连接套接字，并从epoll事件表中删除相应事件 */
void WebServer::closeConn_(HTTPconnection* client)
{
    assert(client);
    //std::cout<<"client"<<client->getFd()<<" quit!"<<std::endl;
    epoller_->delFd(client->getFd());
    client->closeHTTPConn();
}

/* 为连接注册事件和设置计时器 */
void WebServer::addClientConnection(int fd, sockaddr_in addr)
{
    assert(fd>0);
    //users是哈希表，套接字是键，HttpConnect对象是值
    //将fd和连接地址传入,初始化HttpConnect对象，用client表示
    users_[fd].initHTTPConn(fd,addr);
    //添加计时器，到期关闭连接
    if(timeoutMS_>0)
    {
        timer_->addTimer(fd,timeoutMS_,std::bind(&WebServer::closeConn_,this,&users_[fd]));
    }
    epoller_->addFd(fd,EPOLLIN | connectionEvent_);
    //套接字设置非阻塞
    setFdNonblock(fd);
}

/* 新建连接套接字，ET模式会一次将连接队列读完 */
void WebServer::handleListen_() {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    do {
        int fd = accept(listenFd_, (struct sockaddr *)&addr, &len);
        if(fd <= 0) { return;}
        else if(HTTPconnection::userCount >= MAX_FD) {
            sendError_(fd, "Server busy!");
            //std::cout<<"Clients is full!"<<std::endl;
            return;
        }
        addClientConnection(fd, addr);
    } while(listenEvent_ & EPOLLET);
}

/* 将读函数和参数用std::bind绑定，加入线程池的任务队列 */
void WebServer::handleRead_(HTTPconnection* client) {
    assert(client);
    extentTime_(client);
    //非静态成员函数需要传递this指针作为第一个参数
    threadpool_->submit(std::bind(&WebServer::onRead_, this, client));
}

/* 将写函数和参数用std::bind绑定，加入线程池的任务队列 */
void WebServer::handleWrite_(HTTPconnection* client)
{
    assert(client);
    extentTime_(client);
    //非静态成员函数需要传递this指针作为第一个参数
    threadpool_->submit(std::bind(&WebServer::onWrite_, this, client));
}

//重置计时器
void WebServer::extentTime_(HTTPconnection* client)
{
    assert(client);
    if(timeoutMS_>0)
    {
        timer_->update(client->getFd(),timeoutMS_);
    }
}

/* 读函数：先接收再处理 */
void WebServer::onRead_(HTTPconnection* client) 
{
    assert(client);
    int ret = -1;
    int readErrno = 0;
    ret = client->readBuffer(&readErrno);
    //std::cout<<ret<<std::endl;

    //客户端发送EOF
    if(ret <= 0 && readErrno != EAGAIN) {
        //std::cout<<"do not read data!"<<std::endl;
        closeConn_(client);
        return;
    }
    onProcess_(client);
}

/* 处理函数：判断读入的请求报文是否完整，决定是继续监听读还是监听写 */
//如果请求不完整，继续读，如果请求完整，则根据请求内容生成相应的响应报文，并发送
//oneshot需要再次监听
void WebServer::onProcess_(HTTPconnection* client) 
{
    if(client->handleHTTPConn()) {
        epoller_->modFd(client->getFd(), connectionEvent_ | EPOLLOUT);
    }
    else {
        epoller_->modFd(client->getFd(), connectionEvent_ | EPOLLIN);
    }
}

/* 写函数：发送响应报文，大文件需要分多次发送 */
//由于设置了oneshot，需要再次监听读
void WebServer::onWrite_(HTTPconnection* client) {
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    ret = client->writeBuffer(&writeErrno);
    if(client->writeBytes() == 0) {
        /* 传输完成 */
        if(client->isKeepAlive()) {
            onProcess_(client);
            return;
        }
    }
    //发送失败
    else if(ret < 0) {
        //缓存满导致的，继续监听写
        if(writeErrno == EAGAIN) {
            /* 继续传输 */
            epoller_->modFd(client->getFd(), connectionEvent_ | EPOLLOUT);
            return;
        }
    }
    //其他原因导致，关闭连接
    closeConn_(client);
}
bool WebServer::initSocket_() {
    int ret;
    struct sockaddr_in addr;
    if(port_ > 65535 || port_ < 1024) {
        //std::cout<<"Port number error!"<<std::endl;
        return false;
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);
    //设置optLinger需要的参数
    struct linger optLinger = { 0 };
    if(openLinger_) {
        /* 优雅关闭: 直到所剩数据发送完毕或超时 */
        //最多等待20s接受客户端关闭确认
        optLinger.l_onoff = 1;
        optLinger.l_linger = 1;
    }

    //创建监听套接字
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if(listenFd_ < 0) {
        //std::cout<<"Create socket error!"<<std::endl;
        return false;
    }

    //套接字设置优雅关闭
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if(ret < 0) {
        close(listenFd_);
        //std::cout<<"Init linger error!"<<std::endl;
        return false;
    }

    int optval = 1;
    //套接字设置端口复用（端口处于TIME_WAIT时，也可以被bind）
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
    if(ret == -1) {
        //std::cout<<"set socket setsockopt error !"<<std::endl;
        close(listenFd_);
        return false;
    }

    //套接字绑定端口
    ret = bind(listenFd_, (struct sockaddr *)&addr, sizeof(addr));
    if(ret < 0) {
        //std::cout<<"Bind Port"<<port_<<" error!"<<std::endl;
        close(listenFd_);
        return false;
    }

    //套接字设为可接受连接状态，并指明请求队列大小
    ret = listen(listenFd_, 6);
    if(ret < 0) {
        //printf("Listen port:%d error!\n", port_);
        close(listenFd_);
        return false;
    }

    //向epoll注册监听套接字连接事件
    ret = epoller_->addFd(listenFd_,  listenEvent_ | EPOLLIN);
    if(ret == 0) {
        //printf("Add listen error!\n");
        close(listenFd_);
        return false;
    }
    //套接字设置非阻塞（优雅关闭还是会导致close阻塞）
    setFdNonblock(listenFd_);
    //printf("Server port:%d\n", port_);
    return true;
}

/* 套接字设置非阻塞 */
int WebServer::setFdNonblock(int fd) {
    assert(fd > 0);
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
}
