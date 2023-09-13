<a name="Cie2t"></a>
# 一、前言
在OceanBase的源代码中，存在一个宏[SMART_CALL](https://github.com/oceanbase/oceanbase/blob/master/deps/oblib/src/common/ob_smart_call.h)，它的作用是在栈可能溢出时，将函数运行于新的栈上，从而避免潜在的栈溢出问题的发生，确保程序能够稳定地运行。<br />该宏的核心其实就是[ob_smart_call.cpp](https://github.com/oceanbase/oceanbase/blob/master/deps/oblib/src/common/ob_smart_call.cpp)中实现的jump_call函数，函数声明如下，该函数在新栈地址stack_addr上执行func_函数，参数为arg_。
```cpp
int jump_call(void * arg_, int(*func_) (void*), void* stack_addr)
```
下面将从堆栈调用原理的角度出发，讲解如何实现此栈切换功能。<br />本文中使用的相关代码与讲解代码见仓库：[SMART_CALL](https://github.com/zhjc1124/SMART_CALL)
<a name="DQHP6"></a>
# 二、函数调用原理
本章将从x86汇编代码的角度出发，介绍函数调用的原理，并以此为基础分析SMART_CALL的原理。对于已经熟悉这部分内容的读者，可以跳过此章。对于初次接触汇编与函数调用过程的读者，有兴趣深入了解可以阅读经典书籍《深入理解计算机系统》的第三章内容，以更好地理解具体原理。
<a name="ComD0"></a>
## 2.1 程序示例
用一个简单的c程序来看一下函数调用的过程。
```cpp
#include <stdio.h>

int func(int a, int b, int c)
{
    a = 100;
    return a + b + c;  
} 

int main(int argc, char *argv[])
{
    int a = 1;
    return func(a, 2, 3);
}
```
用gcc命令编译，-g选项使目标文件包含程序的调试信息。
:::tips
gcc  main.c -o main -Wall -g 
:::
使用gdb对main进行反汇编。
:::tips
$gdb main<br />(gdb) start<br />(gdb) disassemble /rm
:::
在运行到相应函数时可以通过disassemble /rm得到相应的汇编代码。
```
Dump of assembler code for function func:
4       {
   0x0000000000400492 <+0>:     55                      push   %rbp
   0x0000000000400493 <+1>:     48 89 e5                mov    %rsp,%rbp
   0x0000000000400496 <+4>:     89 7d fc                mov    %edi,-0x4(%rbp)
   0x0000000000400499 <+7>:     89 75 f8                mov    %esi,-0x8(%rbp)
   0x000000000040049c <+10>:    89 55 f4                mov    %edx,-0xc(%rbp)

5           a = 100;
   0x000000000040049f <+13>:    c7 45 fc 64 00 00 00    movl   $0x64,-0x4(%rbp)

6           return a + b + c;  
=> 0x00000000004004a6 <+20>:    8b 55 fc                mov    -0x4(%rbp),%edx
   0x00000000004004a9 <+23>:    8b 45 f8                mov    -0x8(%rbp),%eax
   0x00000000004004ac <+26>:    01 c2                   add    %eax,%edx
   0x00000000004004ae <+28>:    8b 45 f4                mov    -0xc(%rbp),%eax
   0x00000000004004b1 <+31>:    01 d0                   add    %edx,%eax

7       } 
   0x00000000004004b3 <+33>:    5d                      pop    %rbp
   0x00000000004004b4 <+34>:    c3                      retq   

End of assembler dump.
```
```
(gdb) disassemble /rm
Dump of assembler code for function main:
10      {
   0x00000000004004b5 <+0>:     55                     push   %rbp
   0x00000000004004b6 <+1>:     48 89 e5               mov    %rsp,%rbp
   0x00000000004004b9 <+4>:     48 83 ec 10            sub    $0x10,%rsp

11          int a = 1;
=> 0x00000000004004bd <+8>:     c7 45 fc 01 00 00 00   movl   $0x1,-0x4(%rbp)

12          return func(a, 2, 3);
   0x00000000004004c4 <+15>:    8b 45 fc               mov    -0x4(%rbp),%eax
   0x00000000004004c7 <+18>:    ba 03 00 00 00         mov    $0x3,%edx
   0x00000000004004cc <+23>:    be 02 00 00 00         mov    $0x2,%esi
   0x00000000004004d1 <+28>:    89 c7                  mov    %eax,%edi
   0x00000000004004d3 <+30>:    e8 ba ff ff ff         callq  0x400492 <func>

13      }
   0x00000000004004d8 <+35>:    c9                     leaveq 
   0x00000000004004d9 <+36>:    c3                     retq   

End of assembler dump.
```
下面将以该汇编代码为例，讲解func函数的调用流程。
<a name="Mu98p"></a>
## 2.2 调用流程详解
在汇编代码中，存在一些寄存器，通过对其写入和读取，可以实现相应的功能。完整的寄存器列表可参考：[Guide to x86-64](https://web.stanford.edu/class/cs107/guide/x86-64.html)。以r开头的寄存器是x86-64架构中引入的64位寄存器，其低32位是以e开头的寄存器。这些寄存器是为了扩展原有的32位寄存器而引入的。<br />为了便于对后续原理的理解，这里列举了一些后面会用到的寄存器.

| 寄存器 | 功能 | 低32位 |
| --- | --- | --- |
| %rax | 传递函数返回值，也用作存值的临时存储器 | %eax |
| %rdi | 传递函数第一个参数 | %edi |
| %rsi | 传递函数第二个参数 | %esi |
| %rdx | 传递函数第三个参数 | %edx |
| %rsp | Stack Pointer, SP，指向栈的顶部 | %esp |
| %rbp | Frame Pointer，or Base Prointer, BP，指向栈帧的底部 | %ebp |
| %rip | 	指令寄存器（Instruction pointer），指向下一条即将执行的汇编指令 | %eip |

<a name="pP7cs"></a>
### 2.2.1 传参
调用函数最开始执行的就是传参。
```
  ...
12          return func(a, 2, 3);
   0x00000000004004c4 <+15>:    8b 45 fc               mov    -0x4(%rbp),%eax //变量a的值赋值给%eax
   0x00000000004004c7 <+18>:    ba 03 00 00 00         mov    $0x3,%edx       //传入第三个参数
   0x00000000004004cc <+23>:    be 02 00 00 00         mov    $0x2,%esi       //传入第二个参数
   0x00000000004004d1 <+28>:    89 c7                  mov    %eax,%edi       //传入第一个参数
  ...
```
mov指令用于将源操作数复制到目标寄存器中，因此这四行mov指令的作用是向函数func传递参数。其中第一句将内存地址为％rbp偏移－0x4处的值（这里是变量a的值）复制到寄存器％eax中。<br />%edi、％esi、％edx三个寄存器分别表示函数的前三个参数，函数的前六个参数均有对应的寄存器表示。此外，对于超出6个的参数或进行了特殊的调用约定，可以通过倒序压栈的方式进行传递。
<a name="yjODA"></a>
### 2.2.2 调用func函数
```
	0x00000000004004d3 <+30>:    e8 ba ff ff ff         callq  0x400492 <func>
```
调用函数使用的是callq指令，但实际上等价为两个指令：<br />1、将%rip，即下一条指令0x4004d8入栈，被调函数在返回后将取这条指令继续执行<br />2、跳转到目标函数的指令<br />因此上述的等价指令为：
```
push %rip                       //下一条指令入栈
jmpq 0x400492 <func>            //跳转到func的指令
```
<a name="yiGhj"></a>
### 2.2.3 创建func栈帧
```
Dump of assembler code for function func:
4       {
   0x0000000000400492 <+0>:     55                      push   %rbp      //旧rbp压栈
   0x0000000000400493 <+1>:     48 89 e5                mov    %rsp,%rbp //将栈顶赋值给rbp，创建新栈帧
...
```
每个函数执行时都会创建一个栈帧（stack frame)，其空间位于％rbp到％rsp之间，由当前函数持有，在该空间存放函数的局部变量的值。<br />每次创建栈帧时，会记录当前%rbp的值，也就是外层函数的栈帧，这里是main函数的栈帧。然后将%rsp的值复制给%rbp，意味着创建了新栈帧。<br />其实对比main函数创建栈帧的过程可能会发现，main函数创建栈帧有一个将%rsp值减少16的过程，main的栈帧空间即%rbp到%rsp之间的空间为16个字节。这是因为func函数是最内层调用的函数了，不会再创建新栈帧，func函数里的局部变量在调用后也不需要再保存。编译器直接省略了分配空间这一步，也就是后面的栈空间都为func所有。
```
Dump of assembler code for function main:
10      {
   0x00000000004004b5 <+0>:     55                     push   %rbp        //旧rbp压栈
   0x00000000004004b6 <+1>:     48 89 e5               mov    %rsp,%rbp   //将栈底赋值给rbp
   0x00000000004004b9 <+4>:     48 83 ec 10            sub    $0x10,%rsp  //分配16字节栈空间
  ...
```
<a name="yVVDr"></a>
### 2.2.4 创建局部变量
```
...
   0x0000000000400496 <+4>:     89 7d fc                mov    %edi,-0x4(%rbp) //局部变量a
   0x0000000000400499 <+7>:     89 75 f8                mov    %esi,-0x8(%rbp) //局部变量b
   0x000000000040049c <+10>:    89 55 f4                mov    %edx,-0xc(%rbp) //局部变量c
...
```
这里会对函数传进来的参数复制到栈上，相当于局部变量。
<a name="k2W3E"></a>
### 2.2.5 执行功能
```
...
5           a = 100;
   0x000000000040049f <+13>:    c7 45 fc 64 00 00 00    movl   $0x64,-0x4(%rbp) //赋值a = 100;

6           return a + b + c;  
=> 0x00000000004004a6 <+20>:    8b 55 fc                mov    -0x4(%rbp),%edx  //%edx = a
   0x00000000004004a9 <+23>:    8b 45 f8                mov    -0x8(%rbp),%eax  //%eax = b
   0x00000000004004ac <+26>:    01 c2                   add    %eax,%edx        //%edx += %eax 
   0x00000000004004ae <+28>:    8b 45 f4                mov    -0xc(%rbp),%eax  //%eax = c
   0x00000000004004b1 <+31>:    01 d0                   add    %edx,%eax        //%eax += %edx
...
```
func函数的功能比较简单，这里就是利用寄存器计算a+b+c的结果，然后将结果复制到%eax中，作为函数的返回值。
<a name="Wy9NQ"></a>
### 2.2.6 函数返回
```
...
7       } 
   0x00000000004004b3 <+33>:    5d                      pop    %rbp 
   0x00000000004004b4 <+34>:    c3                      retq   

End of assembler dump.
.。。
```
pop %rbp恢复main栈帧的rbp。因为我们的func函数%rsp没有动，所以存的还是上一次main函数的%rbp，直接pop %rbp就好了。对于main函数调用结束后也可以看到是用的leaveq指令，其实比pop %rbp多了一步mov %rbp, %rsp的过程，即回收分配的栈空间。<br />retq相当于pop %rip，也就是将callq时候入栈的%rip指令再取出来写入%rip中，这样下一步就会执行0x4004d8，也就是调用func函数之后的指令。
<a name="u4aJF"></a>
# 三、栈切换方案
<a name="qs5wA"></a>
## 3.1 初步方案
在栈空间即将耗尽时，将函数调用转移至新的栈空间可以避免栈溢出。因此，在掌握函数调用的原理后，需要开始考虑如何实现在新栈上执行函数。<br />一些读者可能很容易就已经想到了一种实现方法，即在调用func函数之前，将旧%rsp压入新栈进行保存，然后将％rsp栈顶指针直接切换到新栈，在新栈上执行函数后，再将栈顶指针切回到旧%rsp值。
```
movq  %rsp, NEW_STACK         //将旧栈地址存入新栈上
movq  NEW_STACK, %rsp         //将栈顶指向新栈
call  FUNC                    //执行FUNC函数
popq  %rsp                    //切回旧栈
```
虽然这种核心思想是正确的，而且实测在没开启编译器优化的情况下，很大一部分情况是可以正常工作的，但这样做仍然存在一些问题需要进行解决。
<a name="DcDxA"></a>
## 3.1 存在的问题
直接切换%rsp寄存器实际上遇到的最主要的问题就是传参是否正常的问题。
<a name="Ao7Yk"></a>
### 3.1.1 rbp栈顶指针的优化
在上面传参的时候，将变量a的值传给第一个参数，用到了%rbp寄存器进行索引局部变量a的值。<br />如果我们的%rsp指向新栈顶，而%rbp不切换仍然指向旧栈帧，这样看上去也并不会出现问题。因为%rbp指向旧栈帧仍然能够索引到局部变量，但在实际过程中，存在着没有使用rbp栈顶指针的情况<br />在x86-64架构下，如果[GCC编译选项](https://gcc.gnu.org/onlinedocs/gcc-4.7.2/gcc/Optimize-Options.html)开启了优化使用 -O1 或 -O2 的情况下或者指定 -fomit-frame-pointer选项，强制GCC不生成栈帧指针，就不会再使用%rbp。这样一来，所有空间在函数开始处就预分配好，不需要栈帧指针，通过%rsp栈顶指针的偏移就可以索引所有的局部变量。<br />所以如果我们切换了%rsp，在开启优化不使用%rbp的情况下，必然会出现索引局部变量失败的问题。
<a name="IQ3rE"></a>
### 3.1.2 超过6个参数的传参
前面也提到了，对于函数传参，前六个参数都有相应的寄存器来传递，超过6个参数的函数会通过倒序压栈的方式来处理。 而且通过__cdecl调用约定设置也会使得函数参数的传参只通过栈进行，而不通过寄存器进行。<br />这些存储在栈上的参数，可能会引起的问题有：

   - 将参数压栈，还是有可能出现栈溢出的问题。
   - 在新栈上执行函数时，需要索引旧栈来获取参数值。但在新栈创建栈帧更改了%rbp后是不可能索引到旧栈的。
<a name="qbeCZ"></a>
## 3.2 解决方案
利用匿名函数和decltype可以将任意函数封装成1个参数的函数，这样可以满足如下两点：<br />1、传参有且仅需要用到一个寄存器%rdi<br />2、不会通过栈进行传参<br />简化版的代码如下所示：
```
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
```
具体来说，这做了两个操作：<br />1、利用匿名函数，定义了一个std::function对象f，它是一个lambda表达式，能够将任意的函数给封装成std::function对象f。<br />2、然后定义了一个函数指针func_，能够将std::function对象转换成一个func_类型的一个函数。<br />这样做可以将任何的函数调用func，转换成一个只有一个参数的函数，函数类型为int(*func_) (void*)，参数类型为void *，即可以通过func_(arg_)进行调用。
<a name="XmH5N"></a>
### 3.3 jump_call具体实现
jump_call实现这里使用了[内联汇编（inline assembly）](https://gcc.gnu.org/onlinedocs/gcc/extensions-to-the-c-language-family/how-to-use-inline-assembly-language-in-c-code.html)来实现jump_call函数。内联汇编也就是在C/C++代码中嵌入汇编代码进行执行的功能。<br />x86的实现如下：
```cpp
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
```
简单来说，内联汇编要按如下规则来写
```cpp
asm ( "assembly code"
           : output operands                  /* optional */
           : input operands                   /* optional */
           : list of clobbered registers      /* optional */
);
```
output operands这里"=a" (ret)  的意思是，"="表示输出，"a"表示使用%eax寄存器，所以这里会将ret的值存储在寄存器%eax中，作为函数的返回值。<br />input operands为"D"(arg_), "r"(func_), "r"(stack_addr)，"D"表示第一个参数%edi，因此会指定arg_存储在%edi寄存器中，func_和stack_addr则会存储在通用寄存器中，这里分别用"%2"和"%3"表示。<br />list of clobbered registers表示可能会修改的寄存器，"memory"是告诉编译器此函数可能会修改内存。
<a name="cCSio"></a>
# 四、踩过的坑
其实jump_call的汇编代码并不是很多，重要的是要搞清楚原理。包括这个代码自己测试也逐步完善了好几版，最初完成的时候99%情况下自测都不会出现问题，但是部署observer的时候反而会core，特别是arm架构的，而且也很难debug出来。这里也把踩过的坑也给记录一下。
<a name="eFo6l"></a>
## 4.1 栈地址对齐问题
栈地址必须16字节对齐，否则会core。<br />所以在新栈上预留空间存储%rsp的时候需要leaq  -0x10(%3), %3预留16个字节来存储旧的%rsp，直接存的话就对不齐了，因为%rsp寄存器是8个字节。<br />新栈的地址在observer上正常情况下是不会出现对不齐的情况，但是偶尔自测的时候取新栈的地址有一定小概率是对不齐的，这时候可以加上andq  $-16, %3进行对齐。
<a name="b5jQV"></a>
## 4.2  开启编译优化时，编译器可能偷懒不传参
最开始的版本我并没有在内联汇编这里指定input和output operands，而是直接复用jump_call传进来的三个参数寄存器进行操作。正常情况下这样确实是没有问题的。但是开启编译优化后，编译器有可能不会将参数通过寄存器传递，而是直接通过%rsp去取参数，因此需要需要显示input和output operands。<br />x86的内联汇编是支持指定传入寄存器和传出寄存器的
```cpp
:"=a" (ret)                                    /* specify rax assigned to ret */
:"D"(arg_), "r"(func_), "r"(stack_addr)        /* specify rdi assigned to arg_ */
```
arm语法有所区别，需要定义寄存器变量：
```cpp
  register int64_t ret __asm__("x0") = 0;          /* specify x0 assigned to ret */
  register void* x0 __asm__("x0") = arg_;          /* specify x0 assigned to arg_ */
  __asm__ __volatile__ (
...
    :"=r" (ret)                                    /* output */
    :"r"(x0), "r"(func_), "r"(stack_addr)          /* input*/   
    :"x9", "memory"                                /* specify x9 is used */
  );
```
<a name="RLXbK"></a>
## 4.3 arm不开启编译优化时不会保存bp寄存器
在不开启编译优化时，需要用到rbp寄存器进行偏移索引局部变量值。但是arm和x86对rbp寄存器的处理时机是不同的。<br />x86的rbp寄存器对应在arm里是x29, x30寄存器。x86在call之前会自动保存rbp寄存器，而arm是在blr调用函数后才进行保存x29和x30，同时如果此时函数内部如果没有再调用其他函数的时候，编译器则不会进行这一步。而我们在这里用在汇编代码里调用函数实际上会让编译器以为这已经是最内层函数了，导致不会对arm的x29，x30寄存器显式保存，所以对于arm我们需要显式保存一下x29，x30
```cpp
	"stp  x29, x30, [%3, #0x10]\n\t"               /* x29, x30 may not be stored in the innest function. */
    ...
    "blr  %2\n\t"                                  /* run the second arg func_ */
    "ldp  x29, x30, [sp, #0x10]\n\t"               /* restore x29, x30 */
```
<a name="lEfK0"></a>
# 五、后记
通过这次SMART_CALL的实现，清清楚楚地把汇编和函数调用过程的原理彻底搞懂了，以前看《深入理解计算机系统》第三章死活看不下去，这次倒是彻底弄明白了。<br />对协程原理比较理解的读者可能也能发现，这个功能其实和协程的实现比较类似。协程也是需要切换栈进行执行另一个函数，区别在于协程的切换需要完整地保存堆栈上下文，在执行完后在恢复上下文，而我们这里的栈切换是不需要这个操作的。<br />对协程感兴趣的可以去看一下[Boost.Context](https://github.com/boostorg/context)里jump和make函数的汇编实现，只有短短20多行的汇编实现了协程的功能，这个也是我们SMART_CALL最初版本调用的库，后面给去掉了。
<a name="f2RMy"></a>
# 参考
1、[oceanbase/deps/oblib/src/common/ob_smart_call.h at master · oceanbase/oceanbase](https://github.com/oceanbase/oceanbase/blob/master/deps/oblib/src/common/ob_smart_call.h)<br />2、《深入理解计算机系统》第三章<br />3、[Guide to x86-64](https://web.stanford.edu/class/cs107/guide/x86-64.html)<br />4、[Optimize Options - Using the GNU Compiler Collection (GCC)](https://gcc.gnu.org/onlinedocs/gcc-4.7.2/gcc/Optimize-Options.html)<br />5、[GCC-Inline-Assembly-HOWTO](https://www.ibiblio.org/gferg/ldp/GCC-Inline-Assembly-HOWTO.html)<br />6、[Boost.Context](https://github.com/boostorg/context)
