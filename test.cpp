#include <iostream>

int main(void)
{
    int *a;
    
    a = new int[10];
    delete[] a;
    
    return 0;
}