#include <iostream>
#include <omp.h>

using namespace std;

int main(void)
{
    omp_set_num_threads(4);
    #pragma omp parallel for
    for (int i=1;i<11;++i)
    {
        int *a = new int[1000000*i];
        delete[] a;
    }
    
    return 0;
}