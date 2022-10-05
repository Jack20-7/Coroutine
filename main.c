#include"coroutine.h"

#include<stdio.h>

struct args{
    int n;
};

static void func(struct scheduler* S,void * ud){
    struct args * arg = ud;
    int start = arg->n;
    int i = 0;
    for(;i < 5;++i){
        printf("coroutine %d : %d\n",coroutine_running(S),start + i);
        //
        coroutine_yield(S);
    }
}

static void test(struct scheduler* S){
   struct args arg1 = { 0 };
   struct args arg2 = { 100 };
   int co1 = coroutine_new(S,func,&arg1);
   int co2 = coroutine_new(S,func,&arg2);

   printf("main start\n");
   while(coroutine_status(S,co1) && coroutine_status(S,co2)){
       coroutine_resume(S,co1); 
       coroutine_resume(S,co2);
   }
}
int main(int argc,char ** argv){
    struct scheduler* S = coroutine_open();

    test(S);

    coroutine_close(S);

    return 0;
}
