#include "rt/ustd.h"
#include "kvm.h"

#ifndef swear

#define swear(b) do {                                                       \
    if (!b) {                                                               \
        printf("%s(%d): assertion %s failed\n", __FILE__, __LINE__, #b);    \
        exit(1);                                                            \
    }                                                                       \
} while (0)

#endif

static uint64_t seed = 1;

static int test0(void) {
    kvm_fixed(int, double, 16) m;
    kvm_init(&m);
    kvm_put(&m, 42, 3.1415);
    double* p = kvm_get(&m, 42);
    printf("m[42]: %f\n", *p);
    bool deleted = kvm_delete(&m, 42);
    printf("deleted: %d\n", deleted);
    return 0;
}

static int test1(void) {
    typedef kvm_fixed(int,   double, 16) kvm_int_double_16;
    typedef kvm_fixed(float, double, 16) kvm_float_double_16;
    {
        kvm_fixed(int, double, 16) m1;
        kvm_init(&m1);
        int i = 123;
        kvm_put(&m1, i, 999.999);
        double* ri = kvm_get(&m1, i);
        printf("ri: %f\n", *ri);
        kvm_delete(&m1, 123);
        ri = kvm_get(&m1, i);
        swear(!ri);
    }
    {
        kvm_fixed(float, double, 16) m2;
        kvm_init(&m2);
        float f = 321.467f;
        kvm_put(&m2, f, 666.666);
        double* rf = kvm_get(&m2, f);
        printf("ri: %f\n", *rf);
        kvm_delete(&m2, 321.467f);
        rf = kvm_get(&m2, 321.467f);
        swear(!rf);
    }
    {
        kvm_heap(uint64_t, uint64_t) m;
        kvm_alloc(&m, 16);
        for (int i = 0; i < 1024; i++) {
            kvm_put(&m, i, i * i);
            swear(*kvm_get(&m, i) == (uint64_t)i * (uint64_t)i);
        }
        kvm_free(&m);
    }
    return 0;
}

static int test2(void) {
    #ifdef DEBUG
    enum { n = 256 };
    #else
    enum { n = 1024 };
    #endif
    typedef kvm_heap(size_t, double) kvm_int_double;
    static double a[n];
    static double b[n];
    for (size_t i = 0; i < n; i++) {
        a[i] = rand64(&seed);
        b[i] = nan("");
    }
    kvm_int_double m;
    kvm_alloc(&m, 4);
    for (int k = 0; k < n * n; k++) {
//      if (k % (16 * 1024) == 0) { printf("%zd\n", k / 1024); }
        size_t i = (size_t)(rand64(&seed) * n);
        swear(i < n);
        switch ((int)(rand64(&seed) * 3)) {
            case 0: {
                kvm_put(&m, i, a[i]);
                double* p = kvm_get(&m, i);
                swear(*p == a[i]);
                b[i] = a[i];
                for (size_t j = 0; j < n; j++) {
                    double* q = kvm_get(&m, j);
                    if (isnan(b[j])) { swear(q == null); } else { swear(*q == b[j]); swear(a[j] == b[j]); }
                }
                break;
            }
            case 1: {
                for (size_t j = 0; j < n; j++) {
                    double* q = kvm_get(&m, j);
                    if (isnan(b[j])) { swear(q == null); } else { swear(*q == b[j]); swear(a[j] == b[j]); }
                }
                double* p = kvm_get(&m, i);
                if (isnan(b[i])) {
                    swear(!p);
                } else {
                    swear(*p == b[i]);
                }
                break;
            }
            case 2: {
                for (size_t j = 0; j < n; j++) {
                    double* q = kvm_get(&m, j);
                    if (isnan(b[j])) { swear(q == null); } else { swear(*q == b[j]); swear(a[j] == b[j]); }
                }
//              if (i == 0x1B5) { debug = true; rt_breakpoint(); }
                if (isnan(b[i])) {
                    swear(!kvm_get(&m, i));
                } else {
                    swear(b[i] == *kvm_get(&m, i));
                }
                bool deleted = kvm_delete(&m, i);
                double* p = kvm_get(&m, i);
                swear(!p);
                if (isnan(b[i])) {
                    swear(!deleted);
                } else {
                    swear(deleted);
                }
                b[i] = nan("");
                for (size_t j = 0; j < n; j++) {
                    double* q = kvm_get(&m, j);
                    if (isnan(b[j])) { swear(q == null); } else { swear(*q == b[j]); swear(a[j] == b[j]); }
                }
                break;
            }
            default: swear(false); break;
        }
    }
    kvm_free(&m);
    return 0;
}

static void permute(size_t index[], size_t n) {
    for (size_t i = 0; i < n; i++) {
        swap(index[i], index[(size_t)(rand64(&seed) * n)]);
    }
}

static int test3(void) {
    enum { n = 16 * 1024 * 1024 };
    static size_t index[n];
    static uint64_t k[n];
    static uint64_t v[n];
    // 75% occupancy:
    static kvm_fixed(uint64_t, uint64_t, n + n / 4) m;
    kvm_init(&m);
    printf("static kvm_fixed(uint64_t, uint64_t, %zd) m;\n", n + n / 4);
    for (size_t i = 0; i < n; i++) {
        index[i] = i;
        k[i] = random64(&seed);
        v[i] = random64(&seed);
    }
    permute(index, n);
    uint64_t t = nanoseconds();
    for (size_t i = 0; i < n; i++) {
        kvm_put(&m, k[index[i]], v[index[i]]);
    }
    t = nanoseconds() - t;
    printf("kvm_put   : %.3f" "\xCE\xBC" "s\n", (t * 1e-3) / (double)n);
    permute(index, n);
    t = nanoseconds();
    for (size_t i = 0; i < n; i++) {
        uint64_t* r = kvm_get(&m, k[index[i]]);
        swear(*r == v[index[i]]);
    }
    t = nanoseconds() - t;
    printf("kvm_get   : %.3f" "\xCE\xBC" "s\n", (t * 1e-3) / (double)n);
    permute(index, n);
    t = nanoseconds();
    for (size_t i = 0; i < n; i++) {
        bool deleted = kvm_delete(&m, k[index[i]]);
        swear(deleted);
    }
    t = nanoseconds() - t;
    printf("kvm_delete: %.3f" "\xCE\xBC" "s\n", (t * 1e-3) / (double)n);
    return 0;
}

static int test4(void) {
    enum { n = 16 * 1024 * 1024 };
    static size_t index[n];
    static uint64_t k[n];
    static uint64_t v[n];
    static kvm_heap(uint64_t, uint64_t) m;
    kvm_alloc(&m, 16);
    printf("static kvm_heap(uint64_t, uint64_t) m;\n", n + n / 4);
    for (size_t i = 0; i < n; i++) {
        index[i] = i;
        k[i] = random64(&seed);
        v[i] = random64(&seed);
    }
    permute(index, n);
    uint64_t t = nanoseconds();
    for (size_t i = 0; i < n; i++) {
        kvm_put(&m, k[index[i]], v[index[i]]);
    }
    t = nanoseconds() - t;
    printf("kvm_put   : %.3f" "\xCE\xBC" "s\n", (t * 1e-3) / (double)n);
    permute(index, n);
    t = nanoseconds();
    for (size_t i = 0; i < n; i++) {
        uint64_t* r = kvm_get(&m, k[index[i]]);
        swear(*r == v[index[i]]);
    }
    t = nanoseconds() - t;
    printf("kvm_get   : %.3f" "\xCE\xBC" "s\n", (t * 1e-3) / (double)n);
    permute(index, n);
    t = nanoseconds();
    for (size_t i = 0; i < n; i++) {
        bool deleted = kvm_delete(&m, k[index[i]]);
        swear(deleted);
    }
    t = nanoseconds() - t;
    printf("kvm_delete: %.3f" "\xCE\xBC" "s\n", (t * 1e-3) / (double)n);
    kvm_free(&m);
    printf("time in " "\xCE\xBC" "s microseconds\n");
    return 0;
}

static void on_signal(int signum) {
    fprintf(stderr, "signal: %d\n", signum);
    exit(EXIT_FAILURE);
}

int main(int argc, const char* argv[]) {
    (void)argc; (void)argv; // unused
    if (signal(SIGABRT, on_signal) == SIG_ERR) {
        perror("signal(SIGABRT, on_signal) failed\n");
    }
    kvm_fatalist = true;
    return test0() || test1() || test2() || test3() || test4();
}
