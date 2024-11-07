#include "rt/ustd.h"
#include <signal.h>
#include <stdlib.h>

#if defined( _MSC_VER ) && ( defined( _M_X64 ) || defined( _M_ARM64 ) )
#include <intrin.h>
#pragma intrinsic(memcpy)
#pragma intrinsic(memcmp)
#endif

bool debug;

#define trace(...) if (debug) { printf(__VA_ARGS__); }

bool kvm_errors_are_fatal; // out of memory and other errors are fatal

#define kvm(tk, tv, _n_)                                \
    struct {                                            \
        uint8_t* pv;                                    \
        uint8_t* pk;                                    \
        uint64_t* bm;                                   \
        size_t a;  /* allocated capacity */             \
        size_t n;  /* number of not empty entries */    \
        uint64_t bitmap[(((_n_ + 7) / 8)|1)];           \
        tv v[(_n_ + (_n_ == 0))];                       \
        tk k[(_n_ + (_n_ == 0))];                       \
}

static bool _kvm_init(void* m, void* k, void* v, size_t n,
                      size_t key_size, size_t val_size,
                      size_t entries) {
    kvm(void*, void*, 0)* pm = m;
    if (entries > 0) { // dynamically allocated map
        pm->pk = malloc(entries * key_size);
        pm->pv = malloc(entries * val_size);
        pm->bm = calloc((entries + 63) / 64, sizeof(uint64_t)); // zero init
        if (!pm->pk || !pm->pv || !pm->bm) {
            free(pm->pk);
            free(pm->pv);
            free(pm->bm);
            if (kvm_errors_are_fatal) { raise(SIGABRT); }
            return false;
        }
        pm->a = entries;
    } else if (n > 1 && entries == 0) {
        pm->a  = 0;
        pm->pk = k;
        pm->pv = v;
        pm->bm = pm->bitmap;
        memset(pm->bitmap, 0, sizeof(pm->bitmap));
    } else { // invalid usage
        if (kvm_errors_are_fatal) { raise(SIGABRT); }
        return false;
    }
    pm->n = 0;
    return true;
}

static void _kvm_free(void* m, size_t n) {
    kvm(void*, void*, 0)* pm = m;
    if (n == 1 && pm->a != 0) {
        free(pm->pk);   pm->pk = 0;
        free(pm->pv);   pm->pv = 0;
        free(pm->bm);   pm->bm = 0;
        pm->a = 0;
    }
}

static inline size_t _kvm_hash(uint64_t key, size_t n) {
    key ^= key >> 33;
    key *= 0XFF51AFD7ED558CCDuLL;
    key ^= key >> 33;
    key *= 0XC4CEB9FE1A85EC53uLL;
    key ^= key >> 33;
    return (size_t)(key % n);
}

static inline bool _kvm_empty(const void* m, size_t i) {
    const kvm(void*, void*, 0)* pm = m;
    return (pm->bm[i / 64] & (1uLL << (i % 64))) == 0;
}

static inline uint64_t _kvm_key(const uint8_t* pkey, size_t key_size) {
    uint64_t key = 0;
    memcpy(&key, pkey, key_size);
    return key;
}

static inline uint64_t _kvm_key_at(const uint8_t* k, size_t key_size, size_t i) {
    return _kvm_key(k + i * key_size, key_size);
}

static inline const void* _kvm_get(const void* m, const size_t n,
                                   const size_t key_size, const size_t val_size,
                                   const void* pkey) {
    const kvm(void*, void*, 0)* pm = m;
    const uint8_t* k = (const uint8_t*)pm->pk;
    const uint8_t* v = (const uint8_t*)pm->pv;
    uint64_t key = _kvm_key(pkey, key_size);
    size_t h = _kvm_hash(key, n);
    size_t i = h; // start
    while (!_kvm_empty(m, i)) {
        if (_kvm_key_at(k, key_size, i) == key) {
            trace("key: 0x%016llX h:%zd n:%zd i:%zd\n", key, h, n, i);
            return v + i * val_size;
        } else {
            i = (i + 1) % n;
            if (i == h) { return 0; }
        }
    }
    trace("key: 0x%016llX h:%zd n:%zd\n", key, h, n);
    return null;
}

static inline bool _kvm_grow(void* m, const size_t key_size, const size_t val_size) {
    kvm(void*, void*, 0)* pm = m;
    if (pm->a >= (size_t)UINT64_MAX / 2) { // ensuing overflow
        if (kvm_errors_are_fatal) { raise(SIGABRT); }
        return false;
    }
    uint8_t*  k = (uint8_t*)pm->pk;
    uint8_t*  v = (uint8_t*)pm->pv;
    size_t    a  = pm->a * 3 / 2;
    uint8_t*  pk = malloc(a * key_size);
    uint8_t*  pv = malloc(a * val_size);
    uint64_t* bm = calloc((a + 63) / 64, sizeof(uint64_t)); // zeros
    if (!pk || !pv || !bm) {
        free(pk);
        free(pv);
        free(bm);
        if (kvm_errors_are_fatal) { raise(SIGABRT); }
        return false;
    } else {
        // rehash all entries into new arrays:
        for (size_t i = 0; i < pm->a; i++) {
            if (!_kvm_empty(m, i)) {
                uint64_t key = _kvm_key_at(k, key_size, i);
                size_t s = _kvm_hash(key, a);
                size_t h = s;
                while ((bm[h / 64] & (1uLL << (h % 64))) != 0) {
                    h = (h + 1) % a;  // new kv map cannot be full
                    assert(h != s);
                }
                memcpy(pk + h * key_size, k + i * key_size, key_size);
                memcpy(pv + h * val_size, v + i * val_size, val_size);
                bm[h / 64] |= (1uLL << (h % 64));
            }
        }
        free(pm->pk); pm->pk = pk;
        free(pm->pv); pm->pv = pv;
        free(pm->bm); pm->bm = bm;
        pm->a = a;
        trace("%zd\n", a);
        return true;
    }
}

static inline bool _kvm_put(void* m, const size_t n_or_a,
                            const size_t key_size, const size_t val_size,
                            const void* pkey, const void* pval) {
    kvm(void*, void*, 0)* pm = m;
    size_t n = n_or_a;
    if (pm->a != 0) {
        assert(pm->a >= 4);
        const size_t n34 = n * 3 / 4;
        if (pm->n >= n34) {
            if (!_kvm_grow(m, key_size, val_size)) {
                if (kvm_errors_are_fatal) { raise(SIGABRT); }
                return false;
            }
            n = pm->a;
        }
    }
    uint8_t* k = (uint8_t*)pm->pk;
    uint8_t* v = (uint8_t*)pm->pv;
    uint64_t key = _kvm_key(pkey, key_size);
    size_t h = _kvm_hash(key, n);
    trace("key: 0x%016llX h:%zd n:%zd\n", key, h, n);
    size_t i = h;
    while (!_kvm_empty(m, i)) {
        if (key == _kvm_key_at(k, key_size, i)) {
            memcpy(k + i * key_size, pkey, key_size);
            memcpy(v + i * val_size, pval, val_size);
            return true;
        } else {
            i = (i + 1) % n;
            if (i == h) {
                if (kvm_errors_are_fatal) { raise(SIGABRT); }
                return false;
            }
        }
    }
    memcpy(k + i * key_size, pkey, key_size);
    memcpy(v + i * val_size, pval, val_size);
    pm->bm[i / 64] |= (1uLL << (i % 64));
    pm->n++;
    return true;
}

static inline void _kvm_remove(void* m, const size_t n,
                               const size_t key_size, const size_t val_size,
                               const size_t at) { // `at` index
    assert(!_kvm_empty(m, at));
    size_t i = at;
    kvm(void*, void*, 0)* pm = m;
    uint8_t* k = (uint8_t*)pm->pk;
    uint8_t* v = (uint8_t*)pm->pv;
    trace("exclude bit: %zd 0x%016llX 0x%016llX\n", i % 64,
        pm->bm[i / 64], pm->bm[i / 64] & ~(1uLL << (i % 64)));
    pm->bm[i / 64] &= ~(1uLL << (i % 64));
    size_t x = i; // next
    for (;;) {
        x = (x + 1) % n;
        if (_kvm_empty(m, x)) { break; }
        size_t h = _kvm_hash(_kvm_key_at(k, key_size, x), n);
        // Check if `h` lies within [i, x), accounting for wrap-around:
        if ((x < i) ^ (h <= i) ^ (h > x)) { // can move
            trace("%zd -> %zd\n", x, i);
            memcpy(k + i * key_size, k + x * key_size, key_size);
            memcpy(v + i * val_size, v + x * val_size, val_size);
            pm->bm[i / 64] |=  (1uLL << (i % 64));
            pm->bm[x / 64] &= ~(1uLL << (x % 64));
            i = x;
        }
    }
    pm->n--;
}

static inline bool _kvm_delete(void* m, const size_t n,
                               size_t key_size, size_t val_size,
                               const void* pkey) {
    const uint8_t* v = _kvm_get(m, n, key_size, val_size, pkey);
    if (v) {
        const kvm(void*, void*, 0)* pm = m;
        size_t i = (v - (const uint8_t*)pm->pv) / val_size;
        trace("key: 0x%016llX h:%zd n:%zd i:%zd\n",
               _kvm_key(pkey, key_size),
               _kvm_hash(_kvm_key(pkey, key_size), n), n, i);
        _kvm_remove(m, n, key_size, val_size, i);
        return true;
    }
    return false;
}

#define kvm_init(m) \
    _kvm_init((void*)m, &(m)->k, &(m)->v,           \
              sizeof((m)->k) / sizeof((m)->k[0]),   \
              sizeof((m)->k[0]), sizeof((m)->v[0]), \
              0)

#define kvm_alloc(m, entries)                       \
    _kvm_init((void*)m, &(m)->k, &(m)->v,           \
              sizeof((m)->k) / sizeof((m)->k[0]),   \
              sizeof((m)->k[0]), sizeof((m)->v[0]), \
              entries)

#define kvm_free(m) _kvm_free((void*)m, sizeof((m)->k) / sizeof((m)->k[0]))

#define kvm_put(m, key, val) _Generic((key),                    \
    char:       _kvm_put,                                       \
    short:      _kvm_put,                                       \
    int:        _kvm_put,                                       \
    long:       _kvm_put,                                       \
    long long:  _kvm_put,                                       \
    unsigned                                                    \
    char:       _kvm_put,                                       \
    unsigned                                                    \
    short:      _kvm_put,                                       \
    unsigned                                                    \
    int:        _kvm_put,                                       \
    unsigned                                                    \
    long:       _kvm_put,                                       \
    unsigned                                                    \
    long long:  _kvm_put,                                       \
    float:      _kvm_put,                                       \
    double:     _kvm_put)                                       \
    (m,                                                         \
     (m)->a > 0 ? (m)->a : sizeof((m)->k) / sizeof((m)->k[0]),  \
     sizeof((m)->k[0]), sizeof((m)->v[0]),                      \
     &(typeof((m)->k[0])){key}, &(typeof((m)->v[0])){val})

#define kvm_get(m, key) (typeof((m)->v[0])*) (_Generic((key),   \
    char:       _kvm_get,                                       \
    short:      _kvm_get,                                       \
    int:        _kvm_get,                                       \
    long:       _kvm_get,                                       \
    long long:  _kvm_get,                                       \
    unsigned                                                    \
    char:       _kvm_get,                                       \
    unsigned                                                    \
    short:      _kvm_get,                                       \
    unsigned                                                    \
    int:        _kvm_get,                                       \
    unsigned                                                    \
    long:       _kvm_get,                                       \
    unsigned                                                    \
    long long:  _kvm_get,                                       \
    float:      _kvm_get,                                       \
    double:     _kvm_get)                                       \
    (m,                                                         \
     (m)->a > 0 ? (m)->a : sizeof((m)->k) / sizeof((m)->k[0]),  \
     sizeof((m)->k[0]), sizeof((m)->v[0]),                      \
     &(typeof((m)->k[0])){key}))


#define kvm_delete(m, key) (_Generic((key),                         \
    char:       _kvm_delete,                                        \
    short:      _kvm_delete,                                        \
    int:        _kvm_delete,                                        \
    long:       _kvm_delete,                                        \
    long long:  _kvm_delete,                                        \
    unsigned                                                        \
    char:       _kvm_delete,                                        \
    unsigned                                                        \
    short:      _kvm_delete,                                        \
    unsigned                                                        \
    int:        _kvm_delete,                                        \
    unsigned                                                        \
    long:       _kvm_delete,                                        \
    unsigned                                                        \
    long long:  _kvm_delete,                                        \
    float:      _kvm_delete,                                        \
    double:     _kvm_delete)                                        \
    (m,                                                             \
     (m)->a > 0 ? (m)->a : sizeof((m)->k) / sizeof((m)->k[0]),      \
     sizeof((m)->k[0]), sizeof((m)->v[0]),                          \
     &(typeof((m)->k[0])){key}))

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
        assert(!ri);
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
        assert(!rf);
    }
    {
        kvm(uint64_t, uint64_t, 0) m;
        kvm_alloc(&m, 16);
        for (int i = 0; i < 1024; i++) {
            kvm_put(&m, i, i * i);
            assert(*kvm_get(&m, i) == (uint64_t)i * (uint64_t)i);
        }
        kvm_free(&m);
    }
    return 0;
}

#ifndef swear

#define swear(b) do {                                                       \
    if (!b) {                                                               \
        printf("%s(%d): assertion %s failed\n", __FILE__, __LINE__, #b);    \
        exit(1);                                                            \
    }                                                                       \
} while (0)

#endif

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
