#ifndef rt_header_included
#define rt_header_included

// Copyright (c) 2024, "Leo" Dmitry Kuznetsov
// This code and the accompanying materials are made available under the terms
// of BSD-3 license, which accompanies this distribution. The full text of the
// license may be found at https://opensource.org/license/bsd-3-clause

// Runtime supplement to make debugging printf easier to locate in source code.
// Defines and Implements null, rt_countof, rt_min/rt_max rt_swap,
// rt_printf, rt_println, rt_assert, rt_swear rt_breakpoint, rt_exit.

// rt_assert(bool, printf_format, ...) extended form is supported.

#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define _CRT_RAND_S // Microsoft rand_s() function
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#if __has_include(<threads.h>) // C11 `optional` threads
#include <threads.h>
#endif

#ifdef _WIN32
#define STRICT
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <Windows.h>
#else // other (posix) platform
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#endif

#ifdef _MSC_VER // overzealous /Wall in cl.exe compiler:
#pragma warning(disable: 4710) // '...': function not inlined
#pragma warning(disable: 4711) // function '...' selected for automatic inline expansion
#pragma warning(disable: 4820) // '...' bytes padding added after data member '...'
#pragma warning(disable: 4996) // The POSIX name for this item is deprecated.
#pragma warning(disable: 5045) // Compiler will insert Spectre mitigation
#pragma warning(disable: 4820) // bytes padding added after data member
#endif

#if defined(_DEBUG) && !defined(DEBUG)
#define DEBUG // clang & gcc toolchains use DEBUG, Microsoft _DEBUG
#endif

#define null ((void*)0) // like null_ptr better than NULL (0)

#define rt_countof(a) (sizeof(a) / sizeof((a)[0]))

#ifndef __cplusplus
#include "rt_generics.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

int32_t rt_exit(int exit_code);

#define rt_printf(...) rt_printf_implementation(__FILE__,           \
                       __LINE__, __func__, false, "" __VA_ARGS__)

#define rt_println(...) rt_printf_implementation(__FILE__,          \
                        __LINE__, __func__, true, "" __VA_ARGS__)


uint64_t rt_nanoseconds(void);

uint64_t rt_random64(uint64_t* state);

double rt_rand64(uint64_t *state); // [0.0..1.0) exclusive to 1.0

int32_t rt_printf_implementation(const char* file, int32_t line,
                                 const char* func, bool append_line_feed,
                                 const char* format,
                                 ...);

#ifdef _WINDOWS_
#define rt_breakpoint() (int)(DebugBreak(), 1)
#else
#define rt_breakpoint() raise(SIGINT)
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#if defined(_MSC_VER)
    #define rt_swear(b, ...) ((void)                                        \
    ((!!(b)) || rt_printf_implementation(__FILE__, __LINE__, __func__,      \
                         true, #b " false " __VA_ARGS__) &&                 \
                rt_breakpoint() && rt_exit(1)))
#else
    #define rt_swear(b, ...) ((void)                                        \
        ((!!(b)) || rt_printf_implementation(__FILE__, __LINE__, __func__,  \
                         true, #b " false " __VA_ARGS__) &&                 \
                rt_breakpoint() && rt_exit(1)))
#endif

#if defined(DEBUG) || defined(_DEBUG)
#define rt_assert(b, ...) rt_swear(b, __VA_ARGS__)
#else
#define rt_assert(b, ...) ((void)(0))
#endif


#endif // rt_header_included

#ifdef rt_implementation

typedef struct rt_debug_output_s {
    char  buffer[8 * 1024];
    char* rd; // read pointer
    char* wr; // write pointer
    int32_t max_prefix_len;
    int32_t max_function_len;
} rt_debug_output_t;

#ifdef __cplusplus
extern "C" {
#endif

static void rt_output_line(const char* s) {
    const char* text = s;
    const char* c = strstr(text, "):");
    if (c != null) {
        while (c > text && *c != '\\') { c--; }
        if (c != text) { text = c + 1; }
    }
    static bool setlocale_called;
    if (!setlocale_called) { setlocale(LC_ALL, "en_US.UTF-8"); }
    #ifdef OutputDebugString // will be defined if Window.h header is included
        if (!setlocale_called) { SetConsoleOutputCP(CP_UTF8); }
        WCHAR utf16[4096];
        int n = MultiByteToWideChar(CP_UTF8, 0, s, -1,
                                    utf16, rt_countof(utf16) - 1);
        if (n > 0) {
            utf16[rt_countof(utf16) - 1] = 0x00;
            OutputDebugStringW(utf16);
        } else {
            OutputDebugStringA("UTF-8 to UTF-16 conversion error\n");
        }
        fprintf(stderr, "%s", text);
    #else
        fprintf(stderr, "%s", text);
    #endif
    setlocale_called = true;
}

static void rt_flush_buffer(rt_debug_output_t* out, const char* file,
                            int32_t line, const char* function) {
    if (out->wr > out->rd) {
        if ((out->wr - out->rd) >= (sizeof(out->buffer) - 4)) {
            strcpy(out->wr - 3, "...\n");
        }
        char prefix[1024];
        snprintf(prefix, sizeof(prefix) - 1, "%s(%d):", file, line);
        prefix[sizeof(prefix) - 1] = 0x00;
        char* start = out->rd;
        char* end = strchr(start, '\n');
        while (end != null) {
            *end = '\0';
            char output[2 * 1024];
            const int32_t pl = (int32_t)strlen(prefix);
            const int32_t fl = (int32_t)strlen(function);
            if (out->max_prefix_len < pl) { out->max_prefix_len = pl; }
            if (out->max_function_len < fl) { out->max_function_len = fl; }
            snprintf(output, sizeof(output) - 1, "%-*s %-*s %s\n",
                     (unsigned int)out->max_prefix_len, prefix,
                     (unsigned int)out->max_function_len, function,
                     start);
            output[sizeof(output) - 1] = 0x00;
            rt_output_line(output);
            start = end + 1;
            end = strchr(start, '\n');
        }
        // Move any leftover text to the beginning of the buffer
        size_t leftover_len = strlen(start);
        memmove(out->buffer, start, leftover_len + 1);
        out->rd = out->buffer;
        out->wr = out->buffer + leftover_len;
    }
}

static int32_t rt_vprintf_implementation(const char* file, int32_t line,
                                         const char* function, bool lf,
                                         const char* format, va_list args) {
    static thread_local rt_debug_output_t out;
    enum { max_width = 1024 };
    static_assert(rt_countof(out.buffer) < INT32_MAX, "32 bit capacity");
    static_assert(max_width < rt_countof(out.buffer) / 2, "too big");
    if (out.rd == null || out.wr == null) {
        out.rd = out.buffer;
        out.wr = out.buffer;
    }
    size_t capacity = sizeof(out.buffer) - (out.wr - out.buffer) - 4;
    // cannot assert here because
    // assert(0 < capacity && capacity < INT32_MAX);
    int32_t n = vsnprintf(out.wr, capacity, format, args);
    if (n < 0) {
        rt_output_line("printf format error\n");
    } else {
        char* p = out.wr + n - 1;
        if (lf) { // called from println() append line feed
            if (n == 0 || *p != '\n') { p++; *p++ = '\n'; *p++ = '\0'; n++; }
            rt_flush_buffer(&out, file, line, function);
        }
        if (n >= (int32_t)capacity) {
            // Handle buffer overflow
            strcpy(out.buffer + sizeof(out.buffer) - 4, "...");
            rt_flush_buffer(&out, file, line, function);
        } else {
            out.wr += n;
            if (strchr(out.wr - n, '\n') != null) {
                rt_flush_buffer(&out, file, line, function);
            } else if ((out.wr - out.rd) >= (size_t)max_width) {
                rt_flush_buffer(&out, file, line, function);
            }
        }
    }
    return n;
}

int32_t rt_printf_implementation(const char* file, int32_t line,
                                 const char* func, bool line_feed,
                                 const char* format,
                                 ...) {
    va_list args;
    va_start(args, format);
    int32_t r = rt_vprintf_implementation(file, line, func, line_feed,
                                               format, args);
    va_end(args);
    return r;
}

#ifdef _MSC_VER
// https://stackoverflow.com/questions/12380603/disable-warning-c4702-seems-not-work-for-vs-2012
#pragma warning(push) // suppress does not work for a function
#pragma warning(disable: 4702) /* unreachable code for rt_exit() function */
#endif

int32_t rt_exit(int exit_code) {
    #ifdef _WINDOWS_
        ExitProcess((UINT)exit_code);
    #else
        exit(exit_code);
    #endif
    return 0;
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

// pure convenience:

uint64_t rt_nanoseconds(void) {
    // Returns nanoseconds since the epoch start, Midnight, January 1, 1970.
    // The value will wrap around in the year ~2554.
    struct timespec ts;
    int r = timespec_get(&ts, TIME_UTC); (void)r;
//  swear(r == TIME_UTC);
    return (ts.tv_sec * 1000000000uLL + ts.tv_nsec);
}

uint64_t rt_random64(uint64_t* state) {
    // Linear Congruential Generator with inline mixing
    thread_local static bool initialized; // must start with ODD seed!
    if (!initialized) { initialized = true; *state |= 1; };
    *state = (*state * 0xD1342543DE82EF95uLL) + 1;
    uint64_t z = *state;
    z = (z ^ (z >> 32)) * 0xDABA0B6EB09322E3uLL;
    z = (z ^ (z >> 32)) * 0xDABA0B6EB09322E3uLL;
    return z ^ (z >> 32);
}

double rt_rand64(uint64_t *state) { // [0.0..1.0) exclusive to 1.0
    return (double)rt_random64(state) / ((double)UINT64_MAX + 1.0);
}

void rt_printf_test_utf8_and_emoji(void) {
    printf("\xF0\x9F\x98\x80 Hello\xF0\x9F\x91\x8B "
           "world\xF0\x9F\x8C\x8D!\n\xF0\x9F\x98\xA1 Goodbye "
           "\xF0\x9F\x98\x88 cruel \xF0\x9F\x98\xB1 "
           "Universe \xF0\x9F\x8C\xA0\xF0\x9F\x8C\x8C..."
           "\xF0\x9F\x92\xA4\n");

    rt_printf("\xF0\x9F\x98\x80 Hello\xF0\x9F\x91\x8B ");
    rt_printf("world\xF0\x9F\x8C\x8D!\n\xF0\x9F\x98\xA1 Goodbye ");
    rt_printf("\xF0\x9F\x98\x88 cruel \xF0\x9F\x98\xB1 ");
    rt_printf("Universe \xF0\x9F\x8C\xA0\xF0\x9F\x8C\x8C...");
    rt_printf("\xF0\x9F\x92\xA4\n");

    rt_printf("\xF0\x9F\x98\x80 Hello\xF0\x9F\x91\x8B "
              "world\xF0\x9F\x8C\x8D!\n\xF0\x9F\x98\xA1 Goodbye "
              "\xF0\x9F\x98\x88 cruel \xF0\x9F\x98\xB1 "
              "Universe \xF0\x9F\x8C\xA0\xF0\x9F\x8C\x8C..."
              "\xF0\x9F\x92\xA4\n");
}

#ifdef __cplusplus
} // extern "C"
#endif


#endif // rt_implementation
