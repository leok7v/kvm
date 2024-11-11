#include "rt/ustd.h"
#include "kvm.h"
#define KMVI_VERIFY
#include "kvmi.h" // with iterator

#include "kvm_1.h" // experimental map

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

static int test1i(void) {
    const char* k[] = {"hello", "good bye"};
    const char* v[] = {"world", "universe"};
    kvmi_fixed(const char*, const char*, 4) m;
    kvmi_init(&m);
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        kvmi_put(&m, k[i], v[i]);
        swear(*kvmi_get(&m, k[i]) == v[i]);
    }
    struct kvmi_iterator iterator = kvmi_iterator(&m);
    while (kvmi_has_next(&iterator)) {
        const char* key = *kvmi_next(&m, &iterator);
        const char* val = *kvmi_get(&m, key);
        printf("\"%s\": \"%s\"\n", key, val);
    }
    iterator = kvmi_iterator(&m);
    while (kvmi_has_next(&iterator)) {
        const char* val = 0;
        const char* key = *kvmi_next_entry(&m, &iterator, &val);
        printf("\"%s\": \"%s\"\n", key, val);
    }
    return 0;
}

typedef kvm_heap(size_t, double) kvm_int_double;

static void test2_verify(kvm_int_double* m, double a[], double b[], size_t n) {
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
                test2_verify(&m, a, b, n);
                break;
            }
            case 1: {
                test2_verify(&m, a, b, n);
                double* p = kvm_get(&m, i);
                if (isnan(b[i])) {
                    swear(!p);
                } else {
                    swear(*p == b[i]);
                }
                break;
            }
            case 2: {
                test2_verify(&m, a, b, n);
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
                test2_verify(&m, a, b, n);
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
    static kvm_fixed(uint64_t, uint64_t, n + n / 4) m;
    kvm_init(&m);
    printf("kvm_fixed(uint64_t, uint64_t, %zd)\n", n + n / 4);
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
    static kvm_heap(uint64_t, uint64_t) m;
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
    map(const char*, const char*) m;
    map_alloc(&m, 4);
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        map_put(&m, k[i], v[i]);
        swear(*map_get(&m, k[i]) == v[i]);
    }
    swear(strcmp(*map_get(&m, hello), v[0]) == 0);
    swear(strcmp(*map_get(&m, good_bye), v[1]) == 0);
    struct map_iterator iterator = map_iterator(&m);
    while (map_has_next(&iterator)) {
        const char* key = *map_next(&m, &iterator);
        const char* val = *map_get(&m, key);
        printf("\"%s\": \"%s\"\n", key, val);
    }
    iterator = map_iterator(&m);
    while (map_has_next(&iterator)) {
        const char* val = 0;
        const char* key = *map_next_entry(&m, &iterator, &val);
        printf("\"%s\": \"%s\"\n", key, val);
    }
    map_free(&m);
    free(hello);
    free(good_bye);
    return 0;
}

static int test6(void) {
    const char* k[] = {"hello", "good bye"};
    const char* v[] = {"world", "universe"};
    char* hello = strdup(k[0]);
    char* good_bye = strdup(k[1]);
    map(const char*, const char*) m;
    map_str(&m, 4);
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        map_put(&m, k[i], v[i]);
        swear(*map_get(&m, k[i]) != v[i]); // duplicated
        swear(strcmp(*map_get(&m, k[i]), v[i]) == 0);
    }
    swear(strcmp(*map_get(&m, hello), v[0]) == 0);
    swear(strcmp(*map_get(&m, good_bye), v[1]) == 0);
    struct map_iterator iterator = map_iterator(&m);
    while (map_has_next(&iterator)) {
        const char* key = *map_next(&m, &iterator);
        const char* val = *map_get(&m, key);
        printf("\"%s\": \"%s\"\n", key, val);
        swear(key != k[0] && key != k[1]); // duplicated
        swear(val != v[0] && val != v[1]); // duplicated
    }
    iterator = map_iterator(&m);
    while (map_has_next(&iterator)) {
        const char* val = 0;
        const char* key = *map_next_entry(&m, &iterator, &val);
        printf("\"%s\": \"%s\"\n", key, val);
    }
    map_clear(&m);
    map_put(&m, null, "Hello");
    map_put(&m, "Hello", null);
    swear(strcmp(*map_get(&m, null), "Hello") == 0);
    swear(*map_get(&m, "Hello") == null);
    map_clear(&m);
    map_put(&m, "", "Hello");
    map_put(&m, "Hello", "");
    swear(strcmp(*map_get(&m, ""), "Hello") == 0);
    swear(strcmp(*map_get(&m, "Hello"), "") == 0);
    map_free(&m);
    free(hello);
    free(good_bye);
    return 0;
}

static int test7(void) {
    enum { n = 1 * 1024 * 1024 };
    static size_t index[n];
    static uint64_t k[n];
    static uint64_t v[n];
    static char ks[n][32];
    static char vs[n][32];
    static map(const char*, const char*) m;
    map_str(&m, 8);
    printf("map(const char*, const char*)\n");
    for (size_t i = 0; i < n; i++) {
        index[i] = i;
        k[i] = random64(&seed);
        v[i] = random64(&seed);
        // UINT64_MAX = 18,446,744,073,709,551,615 (20 decimal digits)
        snprintf(ks[i], sizeof(ks[i]), "%lld", k[i]);
        snprintf(vs[i], sizeof(vs[i]), "%lld", v[i]);
    }
    shuffle(index, n);
    uint64_t t = nanoseconds();
    for (size_t i = 0; i < n; i++) {
        map_put(&m, ks[index[i]], vs[index[i]]);
    }
    t = nanoseconds() - t;
    printf("map_put   : %.3f" "\xCE\xBC" "s\n", (t * 1e-3) / (double)n);
    shuffle(index, n);
    t = nanoseconds();
    for (size_t i = 0; i < n; i++) {
        const char* *r = map_get(&m, ks[index[i]]);
        swear(strcmp(*r, vs[index[i]]) == 0);
    }
    t = nanoseconds() - t;
    printf("map_get   : %.3f" "\xCE\xBC" "s\n", (t * 1e-3) / (double)n);
    shuffle(index, n);
    t = nanoseconds();
    for (size_t i = 0; i < n; i++) {
        bool deleted = map_delete(&m, ks[index[i]]);
        swear(deleted);
    }
    t = nanoseconds() - t;
    printf("map_delete: %.3f" "\xCE\xBC" "s\n", (t * 1e-3) / (double)n);
    map_free(&m);
    printf("time in " "\xCE\xBC" "s microseconds\n");
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
    set_on_signal();
    kvm_fatalist  = true;
    kvmi_fatalist = true;
    map_fatalist  = true;
    return test0() || test1() || test1i() || test2() || test3() ||
           test4() || test5() || test6()  || test7();
}
