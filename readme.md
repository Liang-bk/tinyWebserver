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

## Logger (待完善)

日志：调试，错误定位，数据分析...

要求：不占用主线程的时间去处理日志消息(异步输出)

如何做？主线程(或其他线程)将所写的日志内容先存到阻塞队列中，额外添加一个写线程从阻塞队列中取出内容，写入实际的日志文件

### 单例模式

保证一个类只有一个实例，并提供一个全局访问点，该实例被所有程序模块共享(为了不让在外部创建实例，需要将类的构造函数和析构函数放入private中以防止外界访问)

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
// __VA_ARGS__就是将...复制到自己的位置, 前面加上的##作用为: 当可变参数的个数为0时, 其可以把前面多余的 , 删去, 防止编译错误(宏是单纯的文本替换)
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

日志行数没有写满：问题出在异步这里，前面的writeLog只往队列里塞消息，如果往队列里塞的消息到了MAX条就会新创建一个文件改变文件指针，但是写线程没有反应过来，于是会往新文件里写入原本应该在老文件里的消息

同样的，由于把旧文件的内容写入了新文件，可能导致新文件行数超过规定的最大行数

solution1：将关闭文件的权限交给异步写线程，在队列里附上文件指针指明这是哪一个文件的内容， 当队列中上一个和下一个文件指针不相同时，说明更改了文件，此时关闭上一个文件。

同时为了防止日期导致文件名更改产生的消息归属bug，将两个临界区合为一个，可能会降低效率，但线程安全
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

bug:数据边界不清晰——http_conn在process时未考虑不完整的http包情况



