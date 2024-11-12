#include "rt/rt.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

//#include <algorithm>
#include <unordered_map>
#include <string>

extern "C" {

int kvm_tests(void);
int map_tests(void);

static uint64_t seed;

static void shuffle(size_t index[], size_t n) {
    for (size_t i = 0; i < n; i++) {
        size_t j = (size_t)(rt_rand64(&seed) * (double)n);
        size_t swap = index[i]; index[i] = index[j]; index[j] = swap;
    }
}

static int cpp_test1() {
    constexpr size_t n = 2 * 1024 * 1024;
    static size_t index[n];
    static uint64_t k[n];
    static uint64_t v[n];
    // Using std::unordered_map as the hash table (kvm)
    static std::unordered_map<uint64_t, uint64_t> m;
    m.reserve(n + n / 4); // Reserve space for 75% occupancy
    rt_printf("std::unordered_map<uint64_t, uint64_t> reserved(%zd)\n", n + n / 4);
    for (size_t i = 0; i < n; i++) {
        index[i] = i;
        k[i] = rt_random64(&seed);
        v[i] = rt_random64(&seed);
    }
    shuffle(index, n);
    auto t = rt_nanoseconds();
    for (size_t i = 0; i < n; i++) {
        m[k[index[i]]] = v[index[i]]; // ma_put
    }
    t = rt_nanoseconds() - t;
    rt_printf("unordered_map::put   : %.3f" "\xCE\xBC" "s\n",
              ((double)t * 1e-3) / (double)n);
    shuffle(index, n);
    t = rt_nanoseconds();
    for (size_t i = 0; i < n; i++) {
        uint64_t r = m[k[index[i]]]; // map_get
        rt_swear(r == v[index[i]]);
    }
    t = rt_nanoseconds() - t;
    rt_printf("unordered_map::get   : %.3f" "\xCE\xBC" "s\n",
              ((double)t * 1e-3) / (double)n);

    shuffle(index, n);
    t = rt_nanoseconds();
    for (size_t i = 0; i < n; i++) {
        m.erase(k[index[i]]); // map_delete
    }
    t = rt_nanoseconds() - t;
    rt_printf("unordered_map::delete: %.3f" "\xCE\xBC" "s\n",
              ((double)t * 1e-3) / (double)n);
    return 0;
}

static int cpp_test2(void) {
    enum { n = 1 * 1024 * 1024 };
    static size_t index[n];
    static uint64_t k[n];
    static uint64_t v[n];
    static char ks[n][32];
    static char vs[n][32];
    static std::unordered_map<std::string, std::string> m;
    m.reserve(8); // Reserve 8 entries
    rt_printf("unordered_map<std::string, std::string>\n");
    for (size_t i = 0; i < n; i++) {
        index[i] = i;
        k[i] = rt_random64(&seed);
        v[i] = rt_random64(&seed);
        // UINT64_MAX = 18,446,744,073,709,551,615 (20 decimal digits)
        snprintf(ks[i], sizeof(ks[i]), "%lld", k[i]);
        snprintf(vs[i], sizeof(vs[i]), "%lld", v[i]);
    }
    shuffle(index, n);
    uint64_t t = rt_nanoseconds();
    for (size_t i = 0; i < n; i++) {
        m[ks[index[i]]] = vs[index[i]];
    }
    t = rt_nanoseconds() - t;
    rt_printf("unordered_map::put   : %.3f" "\xCE\xBC" "s\n",
              ((double)t * 1e-3) / (double)n);
    shuffle(index, n);
    t = rt_nanoseconds();
    for (size_t i = 0; i < n; i++) {
        std::string r = m[ks[index[i]]];
        rt_swear(strcmp(r.c_str(), vs[index[i]]) == 0);
    }
    t = rt_nanoseconds() - t;
    rt_printf("unordered_map::get   : %.3f" "\xCE\xBC" "s\n",
        ((double)t * 1e-3) / (double)n);
    shuffle(index, n);
    t = rt_nanoseconds();
    for (size_t i = 0; i < n; i++) {
        m.erase(ks[index[i]]);
    }
    t = rt_nanoseconds() - t;
    rt_printf("unordered_map::delete: %.3f" "\xCE\xBC" "s\n",
        ((double)t * 1e-3) / (double)n);
    rt_printf("time in " "\xCE\xBC" "s microseconds\n");
    return 0;
}


static void on_signal(int signum) {
    fprintf(stderr, "signal: %d\n", signum);
    exit(EXIT_FAILURE);
}

static void set_on_signal(void) {
    if (signal(SIGABRT, on_signal) == SIG_ERR) {
        perror("signal(SIGABRT, on_signal) failed\n");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, const char* argv[]) {
    (void)argc; (void)argv; // unused
    try {
        set_on_signal();
        if (kvm_tests()) { return 1; }
        if (map_tests()) { return 1; }
        if (cpp_test1()) { return 1; }
        if (cpp_test2()) { return 1; }
    } catch (...) {
        return 1;
    }
    return 0;
}

} // extern "C"
