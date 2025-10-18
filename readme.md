## Buffer
Q：为什么需要Buffer？
A：non-blocking模式中，核心思想是避免阻塞在read()/write()或其他IO系统调用上，从而更大限度提高线程的效率，使线程能够服务于多个socket连接。IO线程只阻塞在IO-multiplexing上（select/poll/epoll），因此对于一个socket连接，需要有发送和接收的缓冲：

- 发送时，程序想通过tcp发送100K byte，但在write中，OS只接收80K（操作系统有自己的缓冲控制），程序此时不应该原地阻塞等待发送，应该尽快交出控制权返回eventloop，那么需要有一个Buffer来接管剩余的20K数据，换句话说，程序本身只想尽快的把数据交出去，不关心底层是通过几次write()才完成，那么网络库就需要接管这些数据，保存到output buffer，注册POLLOUT事件，由epoll来通知可写事件，如果己方有数据且可写就发送数据，有数据不可写就交出控制权等待唤醒，无数据时就停止关注POLLOUT，以免造成busyloop。

- 接收时，由于tcp是无边界的字节流协议，容易出现“黏包”现象，接收方必须要处理“收到的数据不构成一条完整的消息”，“一次收到两条消息的数据”等情况

  针对这些情况，接收时一次性将内核缓冲区的数据读完存至input buffer，通过应用程序，由应用程序来判断这些数据能够构成自己想要的一个包，甚至是多个包，如果不够形成一个包，那就不取走数据，如果构成了相应的包，就取走这条消息，并进行相应的处理，重点在不够形成一个包的情况下，因为已经把数据读出来了，但是不够，所以需要找个地方暂存起来。同时一次性把socket中数据读完，是为了防止在LT模式下造成busyloop，在ET模式下造成数据漏读的情况。

### Buffer向外提供的方法
- 获取可读/可写的空间
```c++
size_t readableBytes() const; // 能从buffer取出多少数据
size_t writableBytes() const; // 能向buffer后写入多少数据
size_t prependableBytes() const; // 已经读过了多少数据
```
- 读写指针
```c++
const char* beginWrite() const; // 获取可写空间的位置
char* beginWrite();             // 同上
const char* peek() const;       // 获取读空间的开头
```
- 取走数据(只移动读指针)
```c++
// retrieve系列函数只涉及读指针的改变, 因为没有必要再转成char buffer[]再写到socket中
void retrieve(size_t len);
void retrieveUntil(const char *end);
void retrieveAll();
std::string retrieveAllAsString();
```
- 添加数据
```c++
void append(const char* str, size_t len);
void append(const std::string& str);
void append(const void* data, size_t len);
void append(const Buffer& buff);
```
- 从sockfd中读取数据
```c++
ssize_t readFd(int fd, int *err_flag);
```

## Logger 

日志：调试，错误定位，数据分析...

要求：不占用主线程（在项目中是EventLoop对应线程）的时间去处理日志消息(异步输出)

如何做？主线程(或其他线程)统一将日志内容存到阻塞队列中，日志类启动写线程从阻塞队列中取出内容，写入实际的日志文件

### 单例模式

保证一个类只有一个实例，并提供一个全局访问点，该实例被所有程序模块共享(为了不让在外部创建实例，需要将类的构造函数和析构函数放入private中以防止外界访问)

**懒汉：**静态局部变量（`Logger`实例只有在调用`getInstance`时才被初始化）

**饿汉：**类静态变量（类内定义，类外初始化，因此程序一运行就初始化）

```c++
// 全局访问点, 懒汉模式(只有在getInstance被调用时才创建Logger对象)
// c++ 11后不加锁也能实现线程安全
static Logger *Logger::getInstance() {
    static Logger instance;
    return &instance;
}
```

日志运行流程：

1. 使用单例模式获取Logger的唯一实例
2. 通过实例调用initLogger函数完成初始化，一般设置异步日志，并启动写线程开始从阻塞队列中取出数据放到文件中
3. 主线程（或其他线程）通过宏定义来调用writeLog()创建日志文件并生成日志消息放入阻塞队列，交给写线程处理

宏函数处理：

```c++
// (format, ...)format用于格式化输出, %d, %s之类, ...表示可变参数, 与printf后面带的变量一个作用
#define LOG_BASE(level, format, ...) \
    do { \
        Logger* logger = Logger::getInstance(); \
        if (logger->isOpen() && logger->getLevel() <= level) { \
            logger->writeLog(level, format, ##__VA_ARGS__); \
            logger->flush(); \
        } \
    } while(0);
// __VA_ARGS__就是将...复制到自己的位置, 前面加上的##作用为: 当可变参数的个数为0时, 其可以把前面多余的"," 删去, 防止编译错误(宏是单纯的文本替换)
#define LOG_DEBUG(format, ...) do {LOG_BASE(0, format, ##__VA_ARGS__); } while(0);
#define LOG_INFO(format, ...) do {LOG_BASE(1, format, ##__VA_ARGS__); } while(0);
#define LOG_WARN(format, ...) do {LOG_BASE(2, format, ##__VA_ARGS__); } while(0);
#define LOG_ERROR(format, ...) do {LOG_BASE(3, format, ##__VA_ARGS__); } while(0);
```

### BlockQueue

在普通双端队列的基础上增加了容量限制(max_capacity)，互斥锁(mutex)，和条件变量(conditional_variable)，按照生产者消费者的PV操作来进行队列的访问，每次操作队列时都要加锁

```c++
// producer: push()
// 这里先加锁后使用条件变量, 因为条件变量可以在等待的时候释放锁(和unique_lock配合), 与PV伪代码相反
std::unique_lock<std::mutex> lock(mutex_);
// 生产者生产一条消息
producer_cond_.wait(lock, [this]() {
    return deque_.size() < capacity_ || is_close_;
});
if (is_close_) {
    return;
}
deque_.push_front(item);
consumer_cond_.notify_one();
```

```c++
// consumer: pop()
std::unique_lock<std::mutex> lock(mutex_);
if (consumer_cond_.wait_for(lock, std::chrono::seconds(timeout),[this]() {
    return !deque_.empty() || is_close_;
}) == false) {
    return false;
}
if (is_close_) {
    return false;
}
item = deque_.front();
deque_.pop_front();
producer_cond_.notify_one();
return true;
```

**日志行数没有写满：**问题出在异步这里，前面的writeLog只往队列里塞消息，如果往队列里塞的消息到了MAX条就会新创建一个文件改变文件指针，但是写线程没有反应过来，于是会往新文件里写入原本应该在老文件里的消息

同样的，由于把旧文件的内容写入了新文件，可能导致新文件行数超过规定的最大行数

solution1：将关闭文件的权限交给异步写线程，在队列里附上文件指针指明这是哪一个文件的内容， 当队列中上一个和下一个文件指针不相同时，说明更改了文件，此时关闭上一个文件。

同时为了防止日期导致文件名更改产生的消息归属bug，将两个临界区合为一个，可能会降低效率，但线程安全

**使用ctrl c停止服务时，部分日志消失：**这是由于阻塞队列的实现有问题，在阻塞队列关闭后，对应阻塞的生产者所带的日志一定消失（根本没有进阻塞队列），但阻塞队列中未取出的日志不应该消失，但阻塞队列的实现（在检查到close标志时pop返回false，对应的写线程在发现不能继续取出数据的时候就会终止掉）忽略这一问题，这会导致文件等资源不能正常关闭，应该修正阻塞队列，使其能够在关闭后仍能让消费者消费掉剩余的数据

## 连接池(Pool)

### SQL配置

1. 安装mysql：

   `sudo apt install mysql-server mysql-client libmysqlclient-dev `

2. CMakeLists导入：

   ```cmake
   # 导入mysql头文件所在路径
   include_directories(/usr/include/mysql)
   # 链接mysql库(放在add_executable后面, 不然会丢失目标报错)
   target_link_libraries(${PROJECT_NAME} mysqlclient)
   ```

3. 文件中导入

   ```c++
   #include<mysql/mysql.h>
   ```

同日志系统一样，SQL连接池也采用单例模式，简单易实现，固定连接池中SQL连接的个数，当需要时从中取出，使用完毕后释放，这样在程序初始化后，集中创建并管理多个数据库连接，来保证较快的数据库读取速度（理论上将可以将信号量也使用条件变量代替，之后可以试试，信号量更符合一般教材中的PV操作，易于理解）：

```c++
class SQLConnPool {
public:
    // 单例模式, 全局保留一个实例
    static SQLConnPool* getInstance();
    // 从池中取出一个SQL连接
    MYSQL *getConn();
    // 使用完毕后将该SQL连接放回池
    void freeConn(MYSQL *sql);
    // 池中可用的SQL连接
    int getFreeConnCount();

    /// 初始化连接池
    /// @param host  地址
    /// @param port  端口
    /// @param user  用户名
    /// @param pwd   密码
    /// @param db_name 数据库名称
    /// @param conn_size 连接数量
    void initConnPool(const char *host, int port,
                      const char *user, const char *pwd,
                      const char *db_name, int conn_size);
    // 关闭连接池
    void closeConnPool();
private:
    // 构造函数私有实现
    SQLConnPool();
    ~SQLConnPool();

    // 最大连接数
    int MAX_CONN_;
    // 池队列
    std::queue<MYSQL *> conn_que_;
    std::mutex mutex_;
    // 信号量
    sem_t sem_id_;
};
```

#### RAll

**RAll**代表资源获取就是初始化的意义，是c++管理资源、避免泄露的一种惯用方法。

`unique_ptr`，`lock_guard`都是使用RAll机制来实现。

这里将RAll机制应用到SQL连接池上，避免主动申请SQL连接而忘记释放的问题，更加稳定：

程序中使用`MYSQL *`来表示一个数据库连接的指针，是指针指向该连接，在函数中想要修改一级指针的指向，需要传递二级指针，因为二级指针在解引用时会修改自己指向的地址的内容（也就是能让MYSQL *指针指向正确的SQL连接），否则如果传递一级指针，函数返回后仍然函数内修改了一级指针的指向不会让外部的指针指向改变（c++可以用引用解决这个问题，后面可以修改）

```c++
class SQLConnRAll {
public:
    /// 获取一个SQL连接
    /// @param sql 二级指针是为了改变一级指针指向的位置
    /// @param conn_pool 资源获取地(从池中获取数据)
    SQLConnRAll(MYSQL **sql, SQLConnPool *conn_pool) {
        assert(conn_pool);
        *sql = conn_pool->getConn();
        sql_ = *sql;
        conn_pool_ = conn_pool;
    }
    ~SQLConnRAll() {
        if (sql_) {
            conn_pool_->freeConn(sql_);
        }
    }
private:
    MYSQL *sql_;
    SQLConnPool *conn_pool_;
};
```

### 线程池

同SQL连接池一样，由于线程的创建和销毁都需要消耗不小的系统资源，所以在一开始就创建好一定个数的线程，等到有任务来临时再移交给其中一个线程进行处理，也是一种经典的空间换时间的方法。

```c++
struct Pool {
    std::mutex mutex;// 互斥锁, 互斥取任务队列
    std::condition_variable condition;	// 条件变量, 唤醒和阻塞线程 
    std::queue<std::function<void()>> tasks;	// 任务队列
    bool is_close;	// 线程池是否已关闭
};
```

线程池在初始化时就启动n个线程，每个线程是死循环（直到`is_close`为`true`）时退出。启动流程如下：

1. 尝试获取锁，转2
2. 尝试从任务队列取任务（函数），取出任务后释放锁，然后执行，执行后转1（锁只管任务队列的读取），任务队列为空则转3
3. 阻塞自身，等待任务队列有任务时被唤醒

外部添加任务：

```c++
template<typename T>
void addTask(T &&task) {
    // T &&task 如果T是左值, 传进来的就是 T &task(左值的引用)
    //          如果T是右值, 传进来的就是 T task(普通类型)
    std::unique_lock<std::mutex> lock(pool_->mutex);
    // forward保证了右值不会因为传进来的是T task而改变其右值属性
    // 比如外部调用一个addTask([](){})的形式, 传入之后就变成了 function<void()> task,
    // 这是一个左值, forward的作用就是task原来是右值, 现在还是右值
    // emplace 在处理右值的时候直接constructor, 其余和push相同
    pool_->tasks.emplace(std::forward<T>(task));
    // 来了一个任务, 唤醒一个线程处理
    pool_->condition.notify_one();
}
```

## HTTP

数据边界不清晰——http_conn在process时未考虑不完整的http包情况

## Timer



## Epoll

介绍：

epoll是一个IO多路复用器（只针对网络IO），**多路复用**的意思是：复用线程来进行IO，更详细的解释是相比于传统的一个连接一个线程来处理的情况，多路复用可以做到多个连接使用一个线程来处理，其工作机理简单描述为由内核来监视多个socket对应的文件描述符，当发生事件时，将对应的描述符保存在用户传入的数组中，然后再交由用户处理这些socket上发生的事件

为了复用，相关的网络IO操作就必须做到非阻塞读取和写入数据（read和write在读取/写入网络数据时如果无法进行，就要返回`EAGAIN`错误），否则如果读写的系统调用阻塞了线程，线程就无法处理其他连接的数据，也就做不到复用的效果了

### 工作机制

跟epoll相关的三个重要系统调用：

1. `int epoll_create (int __size)`：创建一个epoll文件描述符并返回

   相当于创建了一个集合，可以往集合中增加/删除/修改socket描述符

2. `int epoll_ctl (int __epfd, int __op, int __fd, struct epoll_event *__event)`：将事件和需要的功能注册到epoll文件描述符上，返回结果代表注册是否成功

   - __epfd：即`epoll_create`创建的epoll文件描述符

   - __op：操作类型，由宏定义，比如`EPOLL_CTL_ADD`（添加描述符），`EPOLL_CTL_MOD`（修改描述符），`EPOLL_CTL_DEL`（删除描述符）

   - __event：事件类型，定义如下：

     ```c++
     typedef union epoll_data {
       void *ptr;
       int fd;
       uint32_t u32;
       uint64_t u64;
     } epoll_data_t;
     struct epoll_event {
       uint32_t events;	/* Epoll events */
       epoll_data_t data;	/* User data variable */
     } __EPOLL_PACKED;
     ```

     其中，`events`是监听的事件类型，在下节给出；

     `data`是一个联合体，用户可以选择用其记录socket对应的fd，或者自定义一个结构来管理socket的fd，再用`ptr`指针指向该结构，具有强大的灵活性

3. `int epoll_wait (int __epfd, struct epoll_event *__events, int __maxevents, int __timeout)`：等待监听的事件发生，其返回值代表有多少个socket上发生了事件

   - __events：是一个`epoll_event`类型的数组，是epoll_wait的另一个返回值，当有事件发生时，内核会将就绪的`epoll_event`放入该数组中
   - __maxevents：表示数组的最大长度
   - __timeout：表示epoll_wait这个系统调用会阻塞多长时间（毫秒），若为-1则表示一直阻塞（直到有事件发生）

#### 工作原理

首先调用epoll_create()来创建一个红黑树结构

然后通过epoll_ctl()将相关的socket描述符和要监听的事件在红黑树上进行注册/修改/删除

epoll底层使用事件驱动在内核中维护一个链表来记录就绪的事件，当某个socket发生事件时，通过回调函数，由内核将该事件对应的epoll_event加入就绪事件列表中

最后由用户调用epoll_wait来获取发生了事件集合（由内核将事件就绪链表复制一份给用户数组）

#### 触发方式

##### ET

又叫边缘触发，表现在只有当事件从无变有时，才提醒，比如一个socket上发生了读事件，当epoll_wait取出该事件后，如果没有处理，那么之后epoll不会再次提醒该事件

##### LT

又叫水平触发，表现在只要事件没有被消费，就一直提醒，比如一个socket上发生了读事件，如果第一次epoll_wait取出该事件没有处理，下一次调用epoll_wait，内核仍然会返回该事件提醒用户处理

### event宏定义

关于`epoll_event`中的uint32_t events可以设置的值（宏定义）：

1. EPOLLIN: 表示对应的文件描述符可以读（包括对端套接字正常关闭）。
2. EPOLLOUT: 表示对应的文件描述符可以写。
3. EPOLLRDHUP: 表示对端套接字关闭连接，或者半关闭连接。
4. EPOLLPRI: 表示对应的文件描述符有紧急数据可读（带外数据）。
5. EPOLLERR: 表示对应的文件描述符发生错误。
6. EPOLLHUP: 表示对应的文件描述符被挂起。
7. EPOLLET: 表示将文件描述符设置为边缘触发（Edge Triggered）模式。
8. EPOLLONESHOT: 表示在处理完一个事件后，自动将文件描述符从 epoll 实例中移除。
9. EPOLLEXCLUSIVE: 表示独占模式，通常用于多线程环境下避免惊群效应。

## webserver

webserver使用单Reactor多线程模式，即主线程处理所有事件（连接事件，客户端读写事件），并将事件对应的处理函数交由线程池处理

流程：

1. 初始化，包括事件模式（ET）和Socket，线程池，数据库连接池，日志，连接数、资源路径等初始化
   - 事件初始化：Server掌管两类事件，server_fd的事件和连进来的client_fd的事件，对于server_fd，主要就是开启ET模式，对于client_fd，包括EPOLLRDHUP（对端半关闭连接），EPOLLONESHOT（事件通知后就将描述符从epoll空间移除），EPOLLET（ET模式）
   - Socket：socket， bind，listen等服务端应有的流程，setsockopt设置端口复用跳过重启的TIME_WAIT时间，向epoll空间注册server_fd的监听事件，并将fd设置为非阻塞式（read/write/accept等IO函数在读不到数据时会立即返回并设置一个错误）
   - 线程池由构造函数直接启动固定数量的线程，数据库连接池和日志由单例模式外部调用初始化函数
