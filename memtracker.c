#define _GNU_SOURCE

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <execinfo.h>
#include <dlfcn.h>
#ifdef _OPENMP
#include <omp.h>
#endif
#if defined __APPLE__ &&  defined __MACH__
#include <malloc/malloc.h>
#elif defined __linux__
#include <malloc.h>
#endif
#include "config.h"

/* set maximum number of threads */
#ifdef _OPENMP
#ifndef MAX_THREADS
#define MAX_THREADS 64
#endif
#else
#ifndef MAX_THREADS
#undef  MAX_THREADS
#endif
#define MAX_THREADS 1
#endif

/* set maximum number of frames to backtrack */
#ifndef MAX_FRAMES
#define MAX_FRAMES 64
#endif

/* set output prefix */
#ifndef PREFIX
#define PREFIX "MEMTRACKER"
#endif

/* size function */
#ifdef __linux__
#define memtr_size(p) malloc_usable_size(p)
#elif defined __APPLE__ &&  defined __MACH__
#define memtr_size(p) malloc_size(p)
#endif

/* thread number function */
#ifdef _OPENMP
#define memtr_thread_num()  omp_get_thread_num()
#define memtr_num_threads() omp_get_num_threads()
#else
#define memtr_thread_num()  0
#define memtr_num_threads() 1
#endif

static void* (*real_malloc)(size_t size) = NULL;
static void  (*real_free)(void *p)       = NULL;
static size_t used_mem[MAX_THREADS];
#define NSIZEPOW 6
static char size_pow[NSIZEPOW] = {'B','K','M','G','T','P'};

static void hread_size(float *out_size, char *unit, const size_t in_size);
static void init_alloc(void);
static int  memtr_printf(const char *format, ...);
static void print_backtrace(unsigned int max_frames);
static void reset_used_mem(void);
static void splash(void);

static void hread_size(float *out_size, char *unit, const size_t in_size)
{
    int i;
    
    *out_size = (float)(in_size);
    
    for (i=0;i<NSIZEPOW;++i)
    {
        if (*out_size < 1000.0)
        {
            *unit     = size_pow[i];
            return;
        }
        *out_size /= 1000.0;
    }
}

static void init_alloc(void)
{
    #pragma omp critical
    {
        if (real_malloc == NULL)
        {
            splash();
            reset_used_mem();
            real_malloc  = dlsym(RTLD_NEXT,"malloc");
            real_free    = dlsym(RTLD_NEXT,"free");
        }
    }
    if (real_malloc == NULL)
    {
        memtr_printf("error in `dlsym`: %s\n", dlerror());
        exit(EXIT_FAILURE);
    }
}

static int memtr_printf(const char * format, ...)
{
    va_list args;
    int status, thread;
    float size;
    char unit;
    
    thread = memtr_thread_num();
    
    hread_size(&size,&unit,used_mem[thread]);
    fprintf(stderr,"%s[thread %d/%d] (used= %6.1f%c): ",PREFIX,thread+1,\
            memtr_num_threads(),size,unit);
    va_start (args, format);
    status = vfprintf(stderr,format,args);
    va_end (args);
    
    return status;
}

static void print_backtrace(unsigned int max_frames)
{
    void* addrlist[max_frames+1];
    int addrlen;
    
    fprintf(stderr,"==== %s BACKTRACE (thread %d/%d)\n",PREFIX,\
            memtr_thread_num()+1,memtr_num_threads());
    addrlen = backtrace(addrlist,(int)(sizeof(addrlist)/sizeof(void*)));
    if (addrlen == 0)
    {
        fprintf(stderr,"error: empty back trace\n");
    }
    else
    {
        backtrace_symbols_fd(addrlist,addrlen,fileno(stderr));
    }
    fprintf(stderr,"===============================\n");
}

static void reset_used_mem(void)
{
    int t;
    
    for (t=0;t<MAX_THREADS;++t)
    {
        used_mem[t] = 0;
    }
}

static void splash(void)
{
    printf("##################################\n");
    printf("# %s v%s started\n",PACKAGE_NAME,PACKAGE_VERSION);
    printf("##################################\n");
}

void *malloc(size_t size)
{
    void *p;
    int thread;
    size_t prev_size;
    
    p      = NULL;
    thread = memtr_thread_num();

    if (thread >= MAX_THREADS)
    {
        memtr_printf("error: MemTrack built with insufficient thread support (MAX_THREADS=%d)\n",\
                     MAX_THREADS);
        exit(EXIT_FAILURE);
    }
    if (real_malloc == NULL)
    {
        init_alloc();
    }
    prev_size = memtr_size(p);
    p = real_malloc(size);
    used_mem[thread] += memtr_size(p) - prev_size;
    #pragma omp critical
    {
        memtr_printf("%6s @ %p | +%luB\n","malloc",p,(long unsigned int)size,\
                     (long unsigned int)used_mem[thread]);
        print_backtrace(MAX_FRAMES);
    }
    
    return p;
}

void free(void *p)
{
    size_t size;
    int thread;
    
    if (p != NULL)
    {
        thread = memtr_thread_num();
        size   = memtr_size(p);
        if (thread >= MAX_THREADS)
        {
            memtr_printf("error: MemTrack built with insufficient thread support (MAX_THREADS=%d)\n",\
                         MAX_THREADS);
            exit(EXIT_FAILURE);
        }
        real_free(p);
        used_mem[thread] -= size;
        #pragma omp critical
        {
            memtr_printf("%6s @ %p | -%luB\n","free",p,(long unsigned int)size,\
                         (long unsigned int)used_mem[thread]);
        }
    }
}
