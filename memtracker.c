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
#define PREFIX "MEMTRACKER: "
#endif

static void* (*real_malloc)(size_t) = NULL;
static void  (*real_free)(void *p)  = NULL;
static size_t used_mem[MAX_THREADS];

static void init_alloc(void);
static int  memtr_printf(const char *format, ...);
static void print_backtrace(unsigned int max_frames);
static void reset_used_mem(void);

static void init_alloc(void)
{
    #pragma omp critical
    {
        if (real_malloc == NULL)
        {
            reset_used_mem();
            real_malloc = dlsym(RTLD_NEXT,"malloc");
            real_free   = dlsym(RTLD_NEXT,"free");
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
    int status;
    
    fprintf(stderr,"%s",PREFIX);
    va_start (args, format);
    status = vfprintf(stderr,format,args);
    va_end (args);
    
    return status;
}

static void print_backtrace(unsigned int max_frames)
{
    void* addrlist[max_frames+1];
    int addrlen;
    
    memtr_printf( "back trace:\n");
    addrlen = backtrace(addrlist,(int)(sizeof(addrlist)/sizeof(void*)));
    if (addrlen == 0)
    {
        memtr_printf("error: empty back trace\n");
        return;
    }
    backtrace_symbols_fd(addrlist,addrlen,fileno(stderr));
}

static void reset_used_mem(void)
{
    int t;
    
    for (t=0;t<MAX_THREADS;++t)
    {
        used_mem[t] = 0;
    }
}

void *malloc(size_t size)
{
    void *p;
    int thread;
    
    p      = NULL;
#ifdef _OPENMP
    thread = omp_get_thread_num();
#else
    thread = 0;
#endif
    
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
    p = real_malloc(size);
#ifdef __linux__
    used_mem[thread] += malloc_usable_size(p);
#elif defined __APPLE__ &&  defined __MACH__
    used_mem[thread] += malloc_size(p);
#endif
    memtr_printf("malloc: %p %lub (used= %lub)\n",p,(long unsigned int)size,\
                 (long unsigned int)used_mem[thread]);
    print_backtrace(MAX_FRAMES);
    
    return p;
}

void free(void *p)
{
    size_t size;
    int thread;
    
#ifdef _OPENMP
    thread = omp_get_thread_num();
#else
    thread = 0;
#endif
#ifdef __linux__
    size = malloc_usable_size(p);
#elif defined __APPLE__ &&  defined __MACH__
    size = malloc_size(p);
#endif
    
    if (thread >= MAX_THREADS)
    {
        memtr_printf("error: MemTrack built with insufficient thread support (MAX_THREADS=%d)\n",\
                     MAX_THREADS);
        exit(EXIT_FAILURE);
    }
    real_free(p);
    used_mem[thread] -= size;
    memtr_printf("free  : %p %lub (used= %lub)\n",p,(long unsigned int)size,\
                 (long unsigned int)used_mem[thread]);
}
