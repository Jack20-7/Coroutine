#include"coroutine.h"

#include<stdio.h>
#include<stdlib.h>
#include<assert.h>
#include<stddef.h>
#include<string.h>
#include<stdint.h>

#if __APPLE__ && __MACH__
    #include<sys/ucontext.h>
#else
    #include<ucontext.h>
#endif

#define STACK_SIZE (1024 * 1024)
#define DEFAULT_COROUTINE 16

struct coroutine;


//协程调度器
struct scheduler{
    char stack[STACK_SIZE];  //运行栈

    ucontext_t main;  //主协程的上下文
    int nco;  //当前存活的协程数目
    int cap;  //协程调度器所能够容纳的最大协程数目
    int running; //当前正在运行的协程id
    struct coroutine** co;  //存放创建好的协程指针
};

//协程
struct coroutine{
    coroutine_func func;
    void * ud;
    ucontext_t ctx;
    struct scheduler* sch; //该协程数据哪一个调度器
    ptrdiff_t cap; //已经分配的内存大小,我的理解就是stack所指向的那一块堆区内存实际大小
    ptrdiff_t size; //协程运行时栈的大小,记录保存的栈的大小,堆区空间中实际保存的数据大小
    int status;
    char * stack;  //协程的运行栈,在切换时对该协程的栈进行保存
};

struct coroutine* _co_new(struct scheduler*S,coroutine_func func,void *ud){
    struct coroutine * co = malloc(sizeof(struct coroutine));
    co->func = func;
    co->ud = ud;
    co->sch = S;
    co->cap = 0;
    co->size = 0;
    co->status = COROUTINE_READY;
    co->stack = NULL;

    return co;
}

void _co_delete(struct coroutine* co){
    free(co->stack);
    free(co);
}

struct scheduler* coroutine_open(void){
    struct scheduler* S = (struct scheduler*)malloc(sizeof(struct scheduler));
    S->nco = 0;
    S->cap = DEFAULT_COROUTINE;
    S->running = -1;
    S->co = (struct coroutine**) malloc(sizeof(struct coroutine*) * S->cap);
    memset(S->co,0,sizeof(struct coroutine*) * S->cap);
    return S;
}

void coroutine_close(struct scheduler* S){
    //对调度器中的协程进行关闭
    int i = 0;
    for(;i < S->cap;++i){
        if(S->co[i]){
            _co_delete(S->co[i]);
        }
    }
    free(S->co);
    S->co = NULL;
    free(S);
}

int coroutine_new(struct scheduler* S,coroutine_func func,void * ud){
    struct coroutine* co = _co_new(S,func,ud);
    if(S->nco >= S->cap){
        //如果当前调度器中的协程数目 >= 容量
        int id = S->cap;
        S->co = realloc(S->co,S->cap * 2 * sizeof(struct coroutine *));
        memset(S->co + S->cap,0,sizeof(struct coroutine*) * S->cap);
        S->co[id] = co;
        S->nco++;
        S->cap *= 2;
        return  id;
    }else{
        int i;
        for(i = 0;i < S->cap;++i){
            //寻找一个空闲位置
            int id = (i + S->nco) % S->cap;
            if(S->co[id] == NULL){
                S->co[id] = co;
                S->nco++;
                return id;
            }
        }
    }
    assert(0);
    return -1;
}

//这里传递的参数是scheduler*类型，之所以用两个32位的int去接收是因为在64位的系统上，指针占用的是8字节，64位，而makecontext所能够接收的参数类型是int类型
static void mainfunc(uint32_t low32,uint32_t hi32){
    uintptr_t ptr = low32 | ((uintptr_t)hi32 << 32);
    struct scheduler* S = (struct scheduler*)ptr;

    int id = S->running;
    struct coroutine* co = S->co[id];
    co->func(S,co->ud);
    _co_delete(co);
    S->co[id] = NULL;
    --S->nco;
    S->running = -1;
}

void coroutine_resume(struct scheduler* S,int id){
    assert(S->running == -1);
    assert(id >= 0 && id < S->cap);

    struct coroutine* co  = S->co[id];
    if(co == NULL){
        return ;
    }

    int status = co->status;
    switch(status){
        case COROUTINE_READY:
            getcontext(&co->ctx);
            co->ctx.uc_stack.ss_sp = S->stack;  //共享栈，所有协程运行时共享调度器的栈
            co->ctx.uc_stack.ss_size = STACK_SIZE;
            co->ctx.uc_link = &S->main;  //该协程执行完毕之后自动跳转到主协程执行
            S->running = id;
            co->status = COROUTINE_RUNNING;

            uintptr_t ptr = (uintptr_t)S;
            makecontext(&co->ctx,(void (*) (void))mainfunc,2,(uint32_t)ptr,
                                            (uint32_t)(ptr >> 32));

            swapcontext(&S->main,&co->ctx);  //切换到co去执行
            //这里最后会切换回来，因为link到的main，main的上下文在这里进行了保存
            break;
        case COROUTINE_SUSPEND:
            //那么就需要对他进行恢复执行
            memcpy(S->stack + STACK_SIZE - co->size,co->stack,co->size);
            S->running = id;
            co->status = COROUTINE_RUNNING;
            swapcontext(&S->main,&co->ctx);
            break;
        default:
            assert(0);
    }
}

//该函数用来对协程co的当前栈的内容进行保存，保存到他自己的stack中
//top代表的是栈的底部，也就是最高地址处
static void _save_stack(struct coroutine* co,char * top){
    char dummy = 0;  //利用linuxOS 中栈 高->低的特性,栈顶在低地址的地方,dummy地址栈顶
    assert(top - &dummy <= STACK_SIZE);
    if(co->cap < (top - &dummy)){
        //如果要保存的栈的大小 > 协程内部专门用来保存栈上下的堆区空间的大小
        free(co->stack);
        co->cap = top - &dummy;
        co->stack = (char*)malloc(co->cap);
    }
    co->size = top - &dummy;
    memcpy(co->stack,&dummy,co->size);
}

//切换回调度器的主协程去执行
void coroutine_yield(struct scheduler* S){
    int id = S->running;
    assert(id >= 0);

    struct coroutine* co = S->co[id]; //获取到当前协程对于的co
    assert((char*)& co > S->stack);

    //保存当前协程栈的上下文
    _save_stack(co,S->stack + STACK_SIZE);
    co->status = COROUTINE_SUSPEND;
    S->running = -1;

    swapcontext(&co->ctx,&S->main);
}

int coroutine_status(struct scheduler* S,int id){
    assert(id >= 0 && id < S->cap);
    if(S->co[id] == NULL){
        return COROUTINE_DEAD;
    }
    return S->co[id]->status;
}

int coroutine_running(struct scheduler* S){
    return S->running;
}
