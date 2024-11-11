#ifndef kvm_h_included
#define kvm_h_included
/*
    # Usage:

    kvm_fatalist = true; // errors will raise SIGABRT before returning false

    ## To create fixed sized map:

    kvm_fixed(int, double, 16) m;

    Declares a fixed-size map `m` with a maximum of 16 entries,
    where keys are integers and values are doubles.

    bool kvm_init(m);

    Initializes the map. Must be called before use. Returns false if fails.

    bool kvm_put(m, key, value);

    Inserts a new key-value pair into the map if the key is not present
    and there is enough space, or replaces the value if the key already exists.

    const value_type* v = kvm_get(m, key);

    Returns null if no pair is associated with the key,
    or a constant pointer to the value otherwise.

    bool kvm_delete(m, key); // returns false if there was no key in the map

    Removes the key-value pair from the map.

    ## To create a dynamically allocated map on the heap:

    kvm_heap(int, double) m;

    bool kvm_alloc(m, 16); // 16 is initial number of entries in the map

    // map will automatically grow as key-value pairs are added

    kvm_free(m); // must be called to free memory

*/

#include <signal.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

bool kvm_fatalist; // any of kvm errors are fatal

#define kvm_fatal(...) do {                              \
    if (kvm_fatalist) {                          \
        fprintf(stderr, "" __VA_ARGS__); raise(SIGABRT); \
    }                                                    \
} while (0)

#define kvm_fixed(tk, tv, _n_)                          \
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

#define kvm_heap(tk, tv) kvm_fixed(tk, tv, 0)

static bool _kvm_init(void* mv, void* k, void* v, size_t n) {
    if (n < 4) {
        kvm_fatal("invalid argument n: %zd minimum 4\n", n);
        return false;
    } else {
        kvm_heap(void*, void*)* m = mv;
        memset(m->bitmap, 0, sizeof(m->bitmap));
        m->a  = 0;
        m->n  = 0;
        m->pk = k;
        m->pv = v;
        m->bm = m->bitmap;
        return true;
    }
}

// `kb` key bytes sizeof(tk) key type
// `vb` val bytes sizeof(tv) val type

static bool _kvm_alloc(void* mv,  size_t kb, size_t vb, size_t n) {
    kvm_heap(void*, void*)* m = mv;
    if (n >= 4) { // dynamically allocated map
        m->pk = malloc(n * kb);
        m->pv = malloc(n * vb);
        m->bm = calloc((n + 63) / 64, sizeof(uint64_t)); // zero init
        if (!m->pk || !m->pv || !m->bm) {
            free(m->pk); free(m->pv); free(m->bm);
            kvm_fatal("out of memory\n");
            return false;
        }
        m->a = n;
        m->n = 0;
        return true;
    } else { // invalid usage
        kvm_fatal("invalid argument n: %zd\n", n);
        return false;
    }
}

static void _kvm_set(void* mv, void* pk, void* pv, void* bm) {
    kvm_heap(void*, void*)* m = mv;
    free(m->pk);   m->pk = pk;
    free(m->pv);   m->pv = pv;
    free(m->bm);   m->bm = bm;
}

static void _kvm_free(void* mv, size_t n) {
    kvm_heap(void*, void*)* m = mv;
    if (n == 1 && m->a != 0) { _kvm_set(mv, 0, 0, 0); m->a = 0; }
}

static inline size_t _kvm_hash(uint64_t key, size_t n) {
    key ^= key >> 33;
    key *= 0XFF51AFD7ED558CCDuLL;
    key ^= key >> 33;
    key *= 0XC4CEB9FE1A85EC53uLL;
    key ^= key >> 33;
    return (size_t)(key % n);
}

#define _kvm_is_empty(m, i) (((m)->bm[(i) / 64] & (1uLL << ((i) % 64))) == 0)

static inline uint64_t _kvm_key(const uint8_t* pkey, const size_t kb) {
    // if compiler propagates constant values of kb to this point
    // it can eliminate sequential ifs and expensive memcpy call
    if (kb == 1) { return *pkey; }
    if (kb == 2) { return *(uint16_t*)pkey; }
    if (kb == 4) { return *(uint32_t*)pkey; }
    if (kb == 8) { return *(uint64_t*)pkey; }
    uint64_t key = 0; memcpy(&key, pkey, kb); return key;
}

static inline uint64_t _kvm_key_at(const uint8_t* k, const size_t kb,
                                   const size_t i) {
    return _kvm_key(k + i * kb, kb);
}

static inline void _kvm_set_at(uint8_t* d, const size_t i,
                              const uint8_t* s, const size_t b) {
    // if compiler propagates constant values of kb to this point
    // it can eliminate sequential ifs and expensive memcpy call
    if (b == 1) { d[i] = *s; return; }
    if (b == 2) { *(uint16_t*)(d + i * b) = *(uint16_t*)s; return; }
    if (b == 4) { *(uint32_t*)(d + i * b) = *(uint32_t*)s; return; }
    if (b == 8) { *(uint64_t*)(d + i * b) = *(uint64_t*)s; return; }
    memcpy(d + i * b, s, b);
}

static inline void _kvm_move(uint8_t* d, const size_t i,
                            const uint8_t* s, const size_t j, const size_t b) {
    _kvm_set_at(d, i, s + j * b, b);
}

const void* _kvm_get(const void* mv, const size_t n,
                     const size_t kb, const size_t vb,
                     const void* pkey) {
    const kvm_heap(void*, void*)* m = mv;
    const uint8_t* k = (const uint8_t*)m->pk;
    const uint8_t* v = (const uint8_t*)m->pv;
    const uint64_t key = _kvm_key(pkey, kb);
    const size_t h = _kvm_hash(key, n);
    size_t i = h; // start
    while (!_kvm_is_empty(m, i)) {
        if (_kvm_key_at(k, kb, i) == key) {
            return v + i * vb;
        } else {
            i = (i + 1) % n;
            if (i == h) { return 0; }
        }
    }
    return 0;
}

static bool _kvm_grow(void* mv, const size_t kb, const size_t vb) {
    kvm_heap(void*, void*)* m = mv;
    if (m->a >= (size_t)(UINTPTR_MAX / 2)) {
        kvm_fatal("allocated overflow: %zd\n", m->a);
        return false;
    }
    uint8_t*  k = (uint8_t*)m->pk;
    uint8_t*  v = (uint8_t*)m->pv;
    size_t    a  = m->a * 3 / 2;
    uint8_t*  pk = malloc(a * kb);
    uint8_t*  pv = malloc(a * vb);
    uint64_t* bm = calloc((a + 63) / 64, sizeof(uint64_t)); // zero init
    if (!pk || !pv || !bm) {
        free(pk); free(pv); free(bm);
        kvm_fatal("out of memory\n");
        return false;
    } else {
        // rehash all entries into new arrays:
        for (size_t i = 0; i < m->a; i++) {
            if (!_kvm_is_empty(m, i)) {
                uint64_t key = _kvm_key_at(k, kb, i);
                size_t h = _kvm_hash(key, a);
                while ((bm[h / 64] & (1uLL << (h % 64))) != 0) {
                    h = (h + 1) % a;  // new kv map cannot be full
                }
                _kvm_move(pk, h, k, i, kb);
                _kvm_move(pv, h, v, i, vb);
                bm[h / 64] |= (1uLL << (h % 64));
            }
        }
        _kvm_set(mv, pk, pv, bm);
        m->a = a;
        return true;
    }
}

bool _kvm_put(void* mv, const size_t n_or_a,
              const size_t kb, const size_t vb,
              const void* pkey, const void* pval) {
    kvm_heap(void*, void*)* m = mv;
    size_t n = n_or_a;
    if (m->a != 0) {
        const size_t n34 = n * 3 / 4;
        if (m->n >= n34) {
            if (!_kvm_grow(mv, kb, vb)) { return false; }
            n = m->a;
        }
    }
    uint8_t* k = (uint8_t*)m->pk;
    uint8_t* v = (uint8_t*)m->pv;
    uint64_t key = _kvm_key(pkey, kb);
    size_t h = _kvm_hash(key, n);
    size_t i = h;
    while (!_kvm_is_empty(m, i)) {
        if (key == _kvm_key_at(k, kb, i)) {
            _kvm_set_at(k, i, pkey, kb);
            _kvm_set_at(v, i, pval, vb);
            return true;
        } else {
            i = (i + 1) % n;
            if (i == h) {
                kvm_fatal("map is full\n");
                return false;
            }
        }
    }
    _kvm_set_at(k, i, pkey, kb);
    _kvm_set_at(v, i, pval, vb);
    m->bm[i / 64] |= (1uLL << (i % 64));
    m->n++;
    return true;
}

bool _kvm_delete(void* mv, const size_t n,
                 size_t kb, size_t vb, const void* pkey) {
    kvm_heap(void*, void*)* m = mv;
    uint8_t* k = (uint8_t*)m->pk;
    uint8_t* v = (uint8_t*)m->pv;
    const uint64_t key = _kvm_key(pkey, kb);
    size_t h = _kvm_hash(key, n);
    bool found = false;
    size_t i = h; // start
    while (!found && !_kvm_is_empty(m, i)) {
        const uint64_t ki = _kvm_key_at(k, kb, i);
        found = ki == key;
        if (!found) {
            i = (i + 1) % n;
            if (i == h) { break; }
        }
    }
    if (found) {
        m->bm[i / 64] &= ~(1uLL << (i % 64));
        size_t x = i;
        for (;;) {
            x = (x + 1) % n;
            if (_kvm_is_empty(m, x)) { break; }
            assert(x != i); // because empty slot exists
            const uint64_t kx = _kvm_key_at(k, kb, x);
            h = _kvm_hash(kx, n);
            const bool can_move = i <= x ? x < h || h <= i :
                                           x < h && h <= i;
            if (can_move) {
                _kvm_move(k, i, k, x, kb);
                _kvm_move(v, i, v, x, vb);
                m->bm[i / 64] |=  (1uLL << (i % 64));
                m->bm[x / 64] &= ~(1uLL << (x % 64));
                i = x;
            }
        }
        m->n--;
    }
    return found;
}

#define kvm_tk(m) typeof((m)->k[0]) // type of key
#define kvm_tv(m) typeof((m)->v[0]) // type of val

#define kvm_ka(m, key) (&(kvm_tk(m)){(key)}) // key address
#define kvm_va(m, val) (&(kvm_tv(m)){(val)}) // val address

#define kvm_kb(m) sizeof((m)->k[0]) // number of bytes in key
#define kvm_vb(m) sizeof((m)->v[0]) // number of bytes in val

#define kvm_fixed_n(m) (sizeof((m)->k) / kvm_kb(m))

#define kvm_capacity(m) ((m)->a > 0 ? (m)->a : kvm_fixed_n(m))

#define kvm_init(m)  _kvm_init(m, &(m)->k, &(m)->v, kvm_fixed_n(m))

#define kvm_alloc(m, n) _kvm_alloc(m, kvm_kb(m), kvm_vb(m), n)

#define kvm_free(m) _kvm_free(m, kvm_fixed_n(m))

#define kvm_put(m, key, val) _kvm_put(m, kvm_capacity(m), \
    kvm_kb(m), kvm_vb(m), kvm_ka(m, key), kvm_va(m, val))

#define kvm_has(m, key) (kvm_tv(m)*)_kvm_has(m, kvm_capacity(m), \
    kvm_kb(m), kvm_vb(m), kvm_ka(m, key))

#define kvm_get(m, key) (kvm_tv(m)*)_kvm_get(m, kvm_capacity(m), \
    kvm_kb(m), kvm_vb(m), kvm_ka(m, key))

#define kvm_delete(m, key) _kvm_delete(m, kvm_capacity(m), \
    kvm_kb(m), kvm_vb(m), kvm_ka(m, key))

#endif // kvm_h_included
