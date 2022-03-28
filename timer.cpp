// encode UTF-8

// @Author        : JenkinsY
// @Date          : 2022-03-28

#include "timer.h"

using namespace std;

/* 数组模拟堆
堆是一个完全二叉树，每个点都小于等于左右结点，根结点也即堆顶上全部数据的最小值（小根堆）
如果用0号作为根结点，那么结点x的左结点是 2x+1，右结点是2x+2
那么结点x的父结点就是 (x-1)/2
swap操作：交换两个结点，同时更新哈希表内每个定时器的下标
down操作：比左或右结点大，与左右结点的最小值交换
up操作：比父结点小，与父结点交换
我们的需求有以下几个：
增加定时器：查哈希表，如果是新定时器，插在最后，再up，如果不是新定时器，就需要调整定时器
调整定时器：更新定时后，再执行down和up（实际上只会执行一个）
删除定时器：与最后一个元素交换，删除末尾元素，然后再down和up（删除任意位置结点，同样也只会执行一个）
*/

void TimerManager::siftup_(size_t i) {
    assert(i >= 0 && i < heap_.size());
    size_t j = (i - 1) / 2;
    while(j >= 0) {
        if(heap_[j] < heap_[i]) { break; }
        swapNode_(i, j);
        i = j;
        j = (i - 1) / 2;
    }
}

void TimerManager::swapNode_(size_t i, size_t j) {
    assert(i >= 0 && i < heap_.size());
    assert(j >= 0 && j < heap_.size());
    swap(heap_[i], heap_[j]);
    ref_[heap_[i].id] = i;
    ref_[heap_[j].id] = j;
} 

bool TimerManager::siftdown_(size_t index, size_t n) {
    assert(index >= 0 && index < heap_.size());
    assert(n >= 0 && n <= heap_.size());
    size_t i = index;
    size_t j = i * 2 + 1;
    while(j < n) {
        if(j + 1 < n && heap_[j + 1] < heap_[j]) j++;
        if(heap_[i] < heap_[j]) break;
        swapNode_(i, j);
        i = j;
        j = i * 2 + 1;
    }
    return i > index;
}

void TimerManager::addTimer(int id, int timeout, const TimeoutCallBack& cb) {
    assert(id >= 0);
    size_t i;
    if(ref_.count(id) == 0) {
        /* 新节点：堆尾插入，调整堆 */
        i = heap_.size();
        ref_[id] = i;
        heap_.push_back({id, Clock::now() + MS(timeout), cb});
        siftup_(i);
    } 
    else {
        /* 已有结点：调整堆 */
        i = ref_[id];
        heap_[i].expire = Clock::now() + MS(timeout);
        heap_[i].cb = cb;
        if(!siftdown_(i, heap_.size())) {
            siftup_(i);
        }
    }
}

void TimerManager::work(int id) {
    /* 删除指定id结点，并触发回调函数 */
    if(heap_.empty() || ref_.count(id) == 0) {
        return;
    }
    size_t i = ref_[id];
    TimerNode node = heap_[i];
    node.cb();
    del_(i);
}

void TimerManager::del_(size_t index) {
    /* 删除指定位置的结点 */
    assert(!heap_.empty() && index >= 0 && index < heap_.size());
    /* 将要删除的结点换到队尾，然后调整堆 */
    size_t i = index;
    size_t n = heap_.size() - 1;
    assert(i <= n);
    if(i < n) {
        swapNode_(i, n);
        if(!siftdown_(i, n)) {
            siftup_(i);
        }
    }
    /* 队尾元素删除 */
    ref_.erase(heap_.back().id);
    heap_.pop_back();
}

void TimerManager::update(int id, int timeout) {
    /* 调整指定id的结点 */
    assert(!heap_.empty() && ref_.count(id) > 0);
    heap_[ref_[id]].expire = Clock::now() + MS(timeout);;
    siftdown_(ref_[id], heap_.size());
}

void TimerManager::handle_expired_event() {
    /* 清除超时结点 */
    if(heap_.empty()) {
        return;
    }
    while(!heap_.empty()) {
        TimerNode node = heap_.front();
        if(chrono::duration_cast<MS>(node.expire - Clock::now()).count() > 0) { 
            break; 
        }
        node.cb();
        pop();
    }
}

void TimerManager::pop() {
    assert(!heap_.empty());
    del_(0);
}

void TimerManager::clear() {
    ref_.clear();
    heap_.clear();
}

int TimerManager::getNextHandle() {
    //处理堆顶计时器，若超时执行回调再删除
    handle_expired_event();
    size_t res = -1;
    if(!heap_.empty()) {
        //计算现在堆顶的超时时间，到期时先唤醒一次epoll，判断是否有新事件（即便已经超时）
        res = chrono::duration_cast<MS>(heap_.front().expire - Clock::now()).count();
        if(res < 0) { res = 0; }
    }
    return res;
}
