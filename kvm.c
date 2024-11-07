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

static int test1(void) {
    typedef kvm(int,   double, 16) kvm_int_double_16;
    typedef kvm(float, double, 16) kvm_float_double_16;
    {
        kvm(int, double, 16) m1;
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
        kvm(float, double, 16) m2;
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
        kvm(uint64_t, uint64_t, 0) m;
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
    uint64_t seed = 1;
    typedef kvm(size_t, double, 0) kvm_int_double;
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

static void on_signal(int signum) {
    fprintf(stderr, "signal: %d\n", signum);
    exit(EXIT_FAILURE);
}

int main(int argc, const char* argv[]) {
    (void)argc; (void)argv; // unused
    if (signal(SIGABRT, on_signal) == SIG_ERR) {
        perror("signal(SIGABRT, on_signal) failed\n");
    }
    kvm_errors_are_fatal = true;
    return test1() || test2();
}
