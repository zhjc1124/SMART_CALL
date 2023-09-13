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