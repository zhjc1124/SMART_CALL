
#include <iostream>
#include <functional>

#define SMART_CALL(func, addr)                                              \
  ({                                                                        \
    int ret = 0         ;                                                   \
      std::function<int()> f = [&]() {                                      \
        int ret = 0;                                                        \
        ret = func;                                                         \
        return ret;                                                         \
      };                                                                    \
      int(*func_) (void*) = [](void *arg) { return (*(decltype(f)*)(arg))(); };\
      void * arg_ = &f;                                                     \
      ret = jump_call(arg_, func_, addr);                                   \
    ret;                                                                    \
  })

#if defined(__x86_64__)
inline int jump_call(void * arg_, int(*func_) (void*), void* stack_addr)
{
  int ret = 0;
  __asm__ __volatile__ ( 
    "leaq  -0x10(%3), %3\n\t"                      /* reserve space for old RSP on new stack, 0x10 for align */
    "movq  %%rsp, (%3)\n\t"                        /* store RSP in new stack */
    "movq  %3, %%rsp\n\t"                          /* jump to new stack */
    "call *%2\n\t"                                 /* run the second arg func_ */
    "popq  %%rsp\n\t"                              /* jump back to old stack */
    :"=a" (ret)                                    /* specify rax assigned to ret */
    :"D"(arg_), "r"(func_), "r"(stack_addr)        /* specify rdi assigned to arg_ */
    :"memory"
  );
  return ret;
}
#elif defined(__aarch64__)
inline int jump_call(void * arg_, int(*func_) (void*), void* stack_addr)
{
  register int64_t ret __asm__("x0") = 0;          /* specify x0 assigned to ret */
  register void* x0 __asm__("x0") = arg_;          /* specify x0 assigned to arg_ */
  __asm__ __volatile__ (
    "sub  %3, %3, #0x20\n\t"                       /* reserve space for old sp on new stack, 0x10 for align */
    "stp  x29, x30, [%3, #0x10]\n\t"               /* x29, x30 may not be stored in the innest function. */
    "mov  x9, sp\n\t"                              /* transit SP by x9 */
    "str  x9, [%3, #0x00]\n\t"                     /* store SP in new stack */          
    "mov  sp, %3\n\t"                              /* jump SP to new stack */
    "blr  %2\n\t"                                  /* run the second arg func_ */
    "ldp  x29, x30, [sp, #0x10]\n\t"               /* restore x29, x30 */
    "ldr  x9, [sp, #0x00]\n\t"                     /* jump back to old stack */
    "mov  sp, x9\n\t"                              /* transit SP by x9 */
    :"=r" (ret)                                    /* output */
    :"r"(x0), "r"(func_), "r"(stack_addr)          /* input*/   
    :"x9", "memory"                                /* specify x9 is used */
  );
  return (int) ret;
}
#else
inline int jump_call(void * arg_, int(*func_) (void*), void* stack_addr)
{
  return func_(arg_);
}
#endif

static constexpr int stack_size = 1024 * 1024 * 2;
char stack1[stack_size];
char stack2[stack_size];

int test(int a, int b, int c){
    return a + b + c;
}

int dec(int &num)
{
  int ret = 0;
  ret = SMART_CALL(test(1, 2, num), (char *)stack2 + stack_size);
  num = ret + 1;
  return ret;
}

int main()
{
  int ret = 0;
  int num = 10;

  ret = SMART_CALL(dec(num), (char *)stack1 + stack_size);       
  std::cout << "ret: " << ret << std::endl;
  std::cout << "num: " << num << std::endl;
  return ret;
}
