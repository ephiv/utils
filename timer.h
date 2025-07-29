#ifndef TIMING_H
#define TIMING_H

#include <time.h>
#include <stdio.h>
#include <stdint.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/time.h>
    #include <unistd.h>
#endif

/* Timer structure to hold timing data */
typedef struct {
    uint64_t start_time;
    uint64_t end_time;
    int is_running;
} timer_t;

/* Get high-resolution timestamp in nanoseconds */
static inline uint64_t get_timestamp_ns(void) {
#ifdef _WIN32
    static LARGE_INTEGER frequency = {0};
    LARGE_INTEGER counter;
    
    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
    }
    
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000000000ULL) / frequency.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec * 1000000000ULL + ts.tv_nsec);
#endif
}

/* Initialize a timer */
static inline void timer_init(timer_t *t) {
    t->start_time = 0;
    t->end_time = 0;
    t->is_running = 0;
}

/* Start timing */
static inline void timer_start(timer_t *t) {
    t->start_time = get_timestamp_ns();
    t->is_running = 1;
}

/* Stop timing */
static inline void timer_stop(timer_t *t) {
    if (t->is_running) {
        t->end_time = get_timestamp_ns();
        t->is_running = 0;
    }
}

/* Get elapsed time in nanoseconds */
static inline uint64_t timer_elapsed_ns(const timer_t *t) {
    if (t->is_running) {
        return get_timestamp_ns() - t->start_time;
    } else {
        return t->end_time - t->start_time;
    }
}

/* Get elapsed time in microseconds */
static inline double timer_elapsed_us(const timer_t *t) {
    return timer_elapsed_ns(t) / 1000.0;
}

/* Get elapsed time in milliseconds */
static inline double timer_elapsed_ms(const timer_t *t) {
    return timer_elapsed_ns(t) / 1000000.0;
}

/* Get elapsed time in seconds */
static inline double timer_elapsed_s(const timer_t *t) {
    return timer_elapsed_ns(t) / 1000000000.0;
}

/* Print elapsed time with appropriate unit */
static inline void timer_print(const timer_t *t, const char *label) {
    uint64_t ns = timer_elapsed_ns(t);
    
    if (label == NULL) label = "Elapsed";
    
    if (ns < 1000) {
        printf("%s: %llu ns\n", label, (unsigned long long)ns);
    } else if (ns < 1000000) {
        printf("%s: %.3f us\n", label, ns / 1000.0);
    } else if (ns < 1000000000) {
        printf("%s: %.3f ms\n", label, ns / 1000000.0);
    } else {
        printf("%s: %.6f s\n", label, ns / 1000000000.0);
    }
}

/* Convenience macros for quick timing */
#define TIMER_START(name) \
    timer_t name; \
    timer_init(&name); \
    timer_start(&name)

#define TIMER_STOP_AND_PRINT(name, label) \
    timer_stop(&name); \
    timer_print(&name, label)

/* Macro for timing a block of code */
#define TIME_BLOCK(label, code) \
    do { \
        TIMER_START(__timer); \
        code; \
        TIMER_STOP_AND_PRINT(__timer, label); \
    } while(0)

/* Simple function timing - times a single function call */
#define TIME_FUNCTION(func_call, label) \
    do { \
        TIMER_START(__timer); \
        func_call; \
        TIMER_STOP_AND_PRINT(__timer, label); \
    } while(0)

/* Benchmark a piece of code by running it multiple times */
static inline double benchmark_code(void (*func)(void), int iterations, const char *label) {
    timer_t t;
    timer_init(&t);
    
    timer_start(&t);
    for (int i = 0; i < iterations; i++) {
        func();
    }
    timer_stop(&t);
    
    double total_ms = timer_elapsed_ms(&t);
    double avg_ms = total_ms / iterations;
    
    if (label) {
        printf("%s: %d iterations, %.6f ms total, %.6f ms average\n", 
               label, iterations, total_ms, avg_ms);
    }
    
    return avg_ms;
}

/* Sleep functions for precise delays */
static inline void sleep_ns(uint64_t nanoseconds) {
#ifdef _WIN32
    /* Windows doesn't have nanosecond precision sleep, use microseconds */
    if (nanoseconds >= 1000) {
        Sleep((DWORD)(nanoseconds / 1000000));
    }
#else
    struct timespec ts;
    ts.tv_sec = nanoseconds / 1000000000ULL;
    ts.tv_nsec = nanoseconds % 1000000000ULL;
    nanosleep(&ts, NULL);
#endif
}

static inline void sleep_us(uint64_t microseconds) {
    sleep_ns(microseconds * 1000);
}

static inline void sleep_ms(uint64_t milliseconds) {
    sleep_ns(milliseconds * 1000000);
}

#endif /* TIMING_H */