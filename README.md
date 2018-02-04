## Buffer_queue
A IO Buffer C lib extract from the implementation of evbuffer in libevent<br>
## Wiki
```
该库的接口实现原理与libevent中evbuffer的实现基本保持一致,部分接口以及线程锁的实现有所不同.
```
## Instructions
```
buffer_queue_new            创建缓存队列
buffer_queue_free           销毁缓存队列
buffer_queue_add            向缓存队列尾部添加数据
buffer_queue_prepend        向缓存队列头部添加数据
buffer_queue_expand             预扩展缓存队列
buffer_queue_copyout            查看缓存中队列数据
buffer_queue_drain          删除缓存队列中数据
buffer_queue_remove             提取缓存队列中数据并删除
buffer_queue_get_length             获取缓存队列数据长度
buffer_queue_stat           打印缓存队列节点信息
buffer_queue_freeze             禁止数据读写
buffer_queue_unfreeze           解除读写限制

BQ_USR_DEF      缓存队列部分参数自定义开关,参数可在[buffer_queue.h](https://github.com/xingshuo/buffer_queue/blob/master/src/buffer_queue.h#L18)中配置,makefile中默认使用自定义参数,可通过注释CFLAGS=-D BQ_USR_DEF恢复libevent配置
```
## Build
```
make
```
## Test
```
./test
```