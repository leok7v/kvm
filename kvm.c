#define UNSTD_NO_RT_IMPLEMENTATION
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
    kvm(int, double, 16) m;
    kvm_alloc(&m);
    kvm_put(&m, 42, 3.1415);
    double* p = kvm_get(&m, 42);
    printf("m[42]: %f\n", *p);
    bool deleted = kvm_delete(&m, 42);
    printf("deleted: %d\n", deleted);
    kvm_free(&m);
    return 0;
}

static int test1(void) {
    typedef kvm(int,   double, 16) kvm_int_double_16;
    typedef kvm(float, double, 16) kvm_float_double_16;
    {
        kvm(int, double, 16) m1; // fixed size
        kvm_alloc(&m1);
        int i = 123;
        kvm_put(&m1, i, 999.999);
        double* ri = kvm_get(&m1, i);
        printf("ri: %f\n", *ri);
        kvm_delete(&m1, 123);
        ri = kvm_get(&m1, i);
        swear(!ri);
        kvm_free(&m1);
    }
    {
        kvm(float, double, 16) m2; // fixed size
        kvm_alloc(&m2);
        float f = 321.467f;
        kvm_put(&m2, f, 666.666);
        double* rf = kvm_get(&m2, f);
        printf("ri: %f\n", *rf);
        kvm_delete(&m2, 321.467f);
        rf = kvm_get(&m2, 321.467f);
        swear(!rf);
        kvm_free(&m2);
    }
    {
        kvm(uint64_t, uint64_t) m; // growing on the heap
        kvm_alloc(&m, 16);
        for (int i = 0; i < 1024; i++) {
            kvm_put(&m, i, i * i);
            swear(*kvm_get(&m, i) == (uint64_t)i * (uint64_t)i);
        }
        kvm_free(&m);
    }
    return 0;
}

typedef kvm(size_t, double) kvm_int_double;

static void test3_verify(kvm_int_double* m, double a[], double b[], size_t n) {
    for (size_t j = 0; j < n; j++) {
        double* q = kvm_get(m, j);
        if (isnan(b[j])) {
            swear(q == null);
        } else {
            swear(a[j] == b[j]);
            swear(*q == b[j]);
        }
    }
}

static int test2(void) {
    #ifdef DEBUG
    enum { n = 256 };
    #else
    enum { n = 1024 };
    #endif
    static double a[n];
    static double b[n];
    for (size_t i = 0; i < n; i++) {
        a[i] = rand64(&seed);
        b[i] = nan("");
    }
    kvm_int_double m;
    kvm_alloc(&m, 4);
    for (int k = 0; k < n * n; k++) {
        size_t i = (size_t)(rand64(&seed) * n);
        swear(i < n);
        switch ((int)(rand64(&seed) * 3)) {
            case 0: {
                kvm_put(&m, i, a[i]);
                double* p = kvm_get(&m, i);
                swear(*p == a[i]);
                b[i] = a[i];
                test3_verify(&m, a, b, n);
                break;
            }
            case 1: {
                test3_verify(&m, a, b, n);
                double* p = kvm_get(&m, i);
                if (isnan(b[i])) {
                    swear(!p);
                } else {
                    swear(*p == b[i]);
                }
                break;
            }
            case 2: {
                test3_verify(&m, a, b, n);
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
                test3_verify(&m, a, b, n);
                break;
            }
            default: swear(false); break;
        }
    }
    kvm_free(&m);
    return 0;
}

static void shuffle(size_t index[], size_t n) {
    for (size_t i = 0; i < n; i++) {
        swap(index[i], index[(size_t)(rand64(&seed) * n)]);
    }
}

static int test3(void) {
    enum { n = 2 * 1024 * 1024 };
    static size_t index[n];
    static uint64_t k[n];
    static uint64_t v[n];
    // 75% occupancy:
    static kvm(uint64_t, uint64_t, n + n / 4) m;
    kvm_alloc(&m);
    printf("kvm(uint64_t, uint64_t, %d)\n", n + n / 4);
    for (size_t i = 0; i < n; i++) {
        index[i] = i;
        k[i] = random64(&seed);
        v[i] = random64(&seed);
    }
    shuffle(index, n);
    uint64_t t = nanoseconds();
    for (size_t i = 0; i < n; i++) {
        kvm_put(&m, k[index[i]], v[index[i]]);
    }
    t = nanoseconds() - t;
    printf("kvm_put   : %.3f" "\xCE\xBC" "s\n", (t * 1e-3) / (double)n);
    shuffle(index, n);
    t = nanoseconds();
    for (size_t i = 0; i < n; i++) {
        uint64_t* r = kvm_get(&m, k[index[i]]);
        swear(*r == v[index[i]]);
    }
    t = nanoseconds() - t;
    printf("kvm_get   : %.3f" "\xCE\xBC" "s\n", (t * 1e-3) / (double)n);
    shuffle(index, n);
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
    enum { n = 2 * 1024 * 1024 };
    static size_t index[n];
    static uint64_t k[n];
    static uint64_t v[n];
    static kvm(uint64_t, uint64_t) m; // heap allocated growing kvm
    kvm_alloc(&m, 16);
    printf("kvm_heap(uint64_t, uint64_t)\n");
    for (size_t i = 0; i < n; i++) {
        index[i] = i;
        k[i] = random64(&seed);
        v[i] = random64(&seed);
    }
    shuffle(index, n);
    uint64_t t = nanoseconds();
    for (size_t i = 0; i < n; i++) {
        kvm_put(&m, k[index[i]], v[index[i]]);
    }
    t = nanoseconds() - t;
    printf("kvm_put   : %.3f" "\xCE\xBC" "s\n", (t * 1e-3) / (double)n);
    shuffle(index, n);
    t = nanoseconds();
    for (size_t i = 0; i < n; i++) {
        uint64_t* r = kvm_get(&m, k[index[i]]);
        swear(*r == v[index[i]]);
    }
    t = nanoseconds() - t;
    printf("kvm_get   : %.3f" "\xCE\xBC" "s\n", (t * 1e-3) / (double)n);
    shuffle(index, n);
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

static int test5(void) {
    const char* k[] = {"hello", "good bye"};
    const char* v[] = {"world", "universe"};
    char* hello = strdup(k[0]);
    char* good_bye = strdup(k[1]);
    kvm(const char*, const char*) m;
    kvm_alloc(&m, 4);
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        kvm_put(&m, k[i], v[i]);
        swear(*kvm_get(&m, k[i]) == v[i]);
    }
    swear(m.n == 2);
    // kvm compares strings as pointers
    swear(!kvm_get(&m, hello));
    swear(!kvm_get(&m, good_bye));
    // use map() to compare keys as zero terminated strings
    kvm_clear(&m);
    swear(m.n == 0);
    kvm_free(&m);
    free(hello);
    free(good_bye);
    return 0;
}

int kvm_tests(void) {
    kvm_fatalist  = true;
    return test0() || test1() || test2() || test3() ||  test4() || test5();
}

#define kvm_implementation
#include "kvm.h" // implement kvm

