#ifndef kvm_h_included
#define kvm_h_included
/*
    Usage:

    kvm(int, double, 16) m;

    Declares fixed map `m` with maximum of 16 entries,
    where keys are integers and values are doubles.

    bool kvm_init(m);

    Must be called to initialize the map.

    bool kvm_put(m, key, value);

    Inserts a new key-value pair into the map if previous
    value for this key was not present and there is enough space
    in the map to insert new pair, or, replaces value in existing
    pair if key value pair is already present in the map.

    const value_type* v = kvm_put(m, key);

    Returns null if there is no pair associated with the key
    in the map or constant pointer to value otherwise.

    bool kvm_delete(m, key);

    Removes key value pair from the map.


    To create dynamically allocated map on the head use:

    kvm(int, double, 0) m;
    bool kvm_alloc(m, 16);
    // map will automatically grow as key value pairs are added
    kvm_free(m, 16); // must call to free memory

*/

#include <signal.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#if defined( _MSC_VER )
#pragma intrinsic(memcpy)
#endif

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
            return v + i * val_size;
        } else {
            i = (i + 1) % n;
            if (i == h) { return 0; }
        }
    }
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
        return true;
    }
}

static inline bool _kvm_put(void* m, const size_t n_or_a,
                            const size_t key_size, const size_t val_size,
                            const void* pkey, const void* pval) {
    kvm(void*, void*, 0)* pm = m;
    size_t n = n_or_a;
    if (pm->a != 0) {
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
    size_t i = at;
    kvm(void*, void*, 0)* pm = m;
    uint8_t* k = (uint8_t*)pm->pk;
    uint8_t* v = (uint8_t*)pm->pv;
    pm->bm[i / 64] &= ~(1uLL << (i % 64));
    size_t x = i; // next
    for (;;) {
        x = (x + 1) % n;
        if (_kvm_empty(m, x)) { break; }
        size_t h = _kvm_hash(_kvm_key_at(k, key_size, x), n);
        // Check if `h` lies within [i, x), accounting for wrap-around:
        if ((x < i) ^ (h <= i) ^ (h > x)) { // can move
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

#define kvm_put(m, key, val) _kvm_put(                              \
    m, (m)->a > 0 ? (m)->a : sizeof((m)->k) / sizeof((m)->k[0]),    \
    sizeof((m)->k[0]), sizeof((m)->v[0]),                           \
    &(typeof((m)->k[0])){key}, &(typeof((m)->v[0])){val})

#define kvm_get(m, key) (typeof((m)->v[0])*)_kvm_get(               \
    m, (m)->a > 0 ? (m)->a : sizeof((m)->k) / sizeof((m)->k[0]),   \
    sizeof((m)->k[0]), sizeof((m)->v[0]),                           \
    &(typeof((m)->k[0])){key})

#define kvm_delete(m, key) _kvm_delete(                             \
    m, (m)->a > 0 ? (m)->a : sizeof((m)->k) / sizeof((m)->k[0]),    \
    sizeof((m)->k[0]), sizeof((m)->v[0]),                           \
    &(typeof((m)->k[0])){key})

#endif // kvm_h_included
