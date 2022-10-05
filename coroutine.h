#ifndef C_COROUTINE_H_
#define C_COROUTINE_H_

//表示协程状态的宏
#define COROUTINE_DEAD 0 //终止态
#define COROUTINE_READY 1 //就绪态
#define COROUTINE_RUNNING 2 //运行态
#define COROUTINE_SUSPEND 3  //暂停态


//只能够进行主协程->子协程之间的切换，不能够进行子协程之间的切换
struct scheduler;  //协程调度器

//第一个参数表示协程所在的协程调度器
//该函数调用的参数
typedef void (* coroutine_func) (struct scheduler* ,void * ud);

struct scheduler* coroutine_open(void);  //创建一个协程调度器

void coroutine_close(struct scheduler*);   //销毁一个协程调度器

// param1 创建的协程所属的调度器
// param2 创建的协程要执行的函数
// param3 调用该函数所需要的参数
// ret 所创建的协程的参数
int coroutine_new(struct scheduler*,coroutine_func,void * ud); //创建一个协程

void coroutine_resume(struct scheduler*,int id); //切换到创建的调度器中指定的协程去执行

int coroutine_status(struct scheduler*,int id);  //返回对于协程的状态

int coroutine_running(struct scheduler*);  //调度器中正在运行的协程id

void coroutine_yield(struct scheduler*);  //切换出协程

#endif
