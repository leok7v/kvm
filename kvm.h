#ifndef kvm_h_included
#define kvm_h_included
/*
    # Usage:

    For all maps initial number of key value pair entries must be >= 4.
    kvm() does not support iterators

    kvm_fatalist = true; // errors will raise SIGABRT before returning false

    ## To create fixed sized map

    kvm(int, double, 16) m; // implementation will not use malloc()/free()

    Declares a fixed-size map `m` with a maximum of 16 entries,
    where keys are integers and values are doubles.

    bool kvm_init(m); // initialize fixed size map

    Initializes the map. Must be called before use.
    Returns false if fails (only on bad arguments and misuse).

    bool kvm_put(m, key, value);

    Inserts a new key-value pair entry into the map if the key was not present
    and there is enough space, or replaces the value if the key already exists.

    printf("map has %zd key value entries\n", m.n);

    value_type* v = kvm_get(m, key);

    Returns null if no pair is associated with the key,
    or a pointer to the value otherwise.

    bool kvm_delete(m, key); // returns false if there was no key in the map

    Removes the key-value pair from the map if present or returns false
    and if there was no key entry.

    kvm_clear(m); // removes all entries from the map

    ## To create a dynamically allocated map on the heap:

    kvm(int, double) m; // the map allocated on the heap and will grow

    bool kvm_alloc(m, 16); // 16 is initial number of map entries on the heap

    // map will automatically grow as key-value pairs are added

    kvm_free(m); // must be called to free memory for heap allocated maps
*/

#include <signal.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

bool kvm_fatalist; // any of kvm errors are fatal

#define kvm_struct(tk, tv, _n_)                         \
    struct {                                            \
        uint8_t*  pv;                                   \
        uint8_t*  pk;                                   \
        uint64_t* bm;                                   \
        size_t    a;  /* allocated capacity */          \
        size_t    n;  /* number of not empty entries */ \
        uint64_t  bitmap[(((_n_ + 7) / 8)|1)];          \
        tv v[(_n_ + (_n_ == 0))];                       \
        tk k[(_n_ + (_n_ == 0))];                       \
}

#ifdef __cplusplus
extern "C" {
#endif

bool _kvm_init(void* mv, size_t kb, size_t vb, size_t n,
               void* k, void* v, size_t c);

bool _kvm_put(void* mv, const size_t c,
              const size_t kb, const size_t vb,
              const void* pkey, const void* pval);

const void* _kvm_get(const void* mv, const size_t c,
                     const size_t kb, const size_t vb,
                     const void* pkey);

bool _kvm_delete(void* mv, const size_t c,
                 size_t kb, size_t vb, const void* pkey);

void _kvm_clear(void* mv, size_t c);

void _kvm_free(void* mv, size_t c);

#ifdef __cplusplus
} // extern "C"
#endif

#define _kvm_2_arg(tk, tv)           kvm_struct(tk, tv, 0)
#define _kvm_3_arg(tk, tv, n)        kvm_struct(tk, tv, n)
#define _kvm_get_4th_arg(arg1, arg2, arg3, arg4, ...) arg4
#define _kvm_chooser(...) _kvm_get_4th_arg(__VA_ARGS__, _kvm_3_arg, _kvm_2_arg, )
#define kvm(...) _kvm_chooser(__VA_ARGS__)(__VA_ARGS__)

#define _kvm_alloc_and_init(m, n) \
    _kvm_init(m, _kvm_kb(m), _kvm_vb(m), n, &(m)->k, &(m)->v, _kvm_fixed_c(m))


#define _kvm_init_1_arg(m)    _kvm_alloc_and_init(m, 0)
#define _kvm_init_2_arg(m, n) _kvm_alloc_and_init(m, n)
#define _kvm_get_3rd_arg(arg1, arg2, arg3, ...) arg3
#define _kvm_init_chooser(...) _kvm_get_3rd_arg(__VA_ARGS__, \
                               _kvm_init_2_arg, _kvm_init_1_arg, )
#define kvm_alloc(...) _kvm_init_chooser(__VA_ARGS__)(__VA_ARGS__)
#define kvm_init(m)    _kvm_alloc_and_init(m, 0)

#define _kvm_tk(m) typeof((m)->k[0]) // type of key
#define _kvm_tv(m) typeof((m)->v[0]) // type of val

#define _kvm_ka(m, key) (&(_kvm_tk(m)){(key)}) // key address
#define _kvm_va(m, val) (&(_kvm_tv(m)){(val)}) // val address

#define _kvm_kb(m) sizeof((m)->k[0]) // number of bytes in key
#define _kvm_vb(m) sizeof((m)->v[0]) // number of bytes in val

#define _kvm_fixed_c(m) (sizeof((m)->k) / _kvm_kb(m))

#define kvm_capacity(m) ((m)->a > 0 ? (m)->a : _kvm_fixed_c(m))

#define kvm_clear(m) _kvm_clear(m, _kvm_fixed_c(m))
#define kvm_free(m)  _kvm_free(m,  _kvm_fixed_c(m))

#define kvm_put(m, key, val) _kvm_put(m, kvm_capacity(m), \
    _kvm_kb(m), _kvm_vb(m), _kvm_ka(m, key), _kvm_va(m, val))

#define kvm_get(m, key) (_kvm_tv(m)*)_kvm_get(m, kvm_capacity(m), \
    _kvm_kb(m), _kvm_vb(m), _kvm_ka(m, key))

#define kvm_delete(m, key) _kvm_delete(m, kvm_capacity(m), \
    _kvm_kb(m), _kvm_vb(m), _kvm_ka(m, key))

#endif // kvm_h_included

#if defined(kvm_implementation) && !defined(kvm_implemented)

#define kvm_implemented

#ifdef __cplusplus
extern "C" {
#endif

#define kvm_fatal_return_zero(...) do {  \
    if (kvm_fatalist) {                  \
        fprintf(stderr, "" __VA_ARGS__); \
        raise(SIGABRT);                  \
    }                                    \
    return 0;                            \
} while (0)

// _t suffixes reserved by posix, but kvm_t is not exposed from implementation

typedef kvm(void*, void*) kvm_t;

// `kb` key bytes sizeof(tk) key type
// `vb` val bytes sizeof(tv) val type

static bool _kvm_alloc(kvm_t* m,  size_t kb, size_t vb, size_t n) {
    if (n >= 4) { // dynamically allocated map
        m->pk = malloc(n * kb);
        m->pv = malloc(n * vb);
        m->bm = calloc((n + 63) / 64, sizeof(uint64_t)); // zero init
        if (!m->pk || !m->pv || !m->bm) {
            free(m->pk); free(m->pv); free(m->bm);
            kvm_fatal_return_zero("out of memory\n");
        }
        m->a = n;
        m->n = 0;
        return true;
    } else { // invalid usage
        kvm_fatal_return_zero("invalid argument n: %zd\n", n);
    }
}

bool _kvm_init(void* mv, size_t kb, size_t vb, size_t n,
               void* k, void* v, size_t c) {
    kvm_t* m = mv;
    if (c == 1) {
        return _kvm_alloc(m, kb, vb, n);
    } else if (n != 0) {
        kvm_fatal_return_zero("invalid argument n: %zd\n", n);
    } else {
        memset(m->bitmap, 0, sizeof(m->bitmap));
        m->a  = 0;
        m->n  = 0;
        m->pk = k;
        m->pv = v;
        m->bm = m->bitmap;
        return true;
    }
}

static void _kvm_set_pointers(kvm_t* m, void* pk, void* pv, void* bm) {
    free(m->pk); m->pk = pk;
    free(m->pv); m->pv = pv;
    free(m->bm); m->bm = bm;
}

void _kvm_clear(void* mv, size_t c) {
    kvm_t* m = mv;
    m->n = 0;
    const size_t capacity = m->a > 0 ? m->a : c;
    memset(m->bm, 0, ((capacity + 63) / 64) * sizeof(m->bm[0]));
}

void _kvm_free(void* mv, size_t c) {
    _kvm_clear(mv, c);
    kvm_t* m = mv;
    if (c == 1 && m->a != 0) { _kvm_set_pointers(m, 0, 0, 0); m->a = 0; }
}

static inline size_t _kvm_hash(uint64_t key, size_t c) {
    key ^= key >> 33;
    key *= 0XFF51AFD7ED558CCDuLL;
    key ^= key >> 33;
    key *= 0XC4CEB9FE1A85EC53uLL;
    key ^= key >> 33;
    return (size_t)(key % c);
}

#define _kvm_bm_incl(bm, i) do { bm[i / 64] |=  (1uLL << (i % 64)); } while (0)
#define _kvm_bm_excl(bm, i) do { bm[i / 64] &= ~(1uLL << (i % 64)); } while (0)
#define _kvm_bm_is_empty(bm, i) ((bm[(i) / 64] & (1uLL << ((i) % 64))) == 0)

#define _kvm_is_empty(m, i) _kvm_bm_is_empty((m)->bm, i)

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

#define _kvm_set_entry(k, v, i, pkey, pval, kb, vb) do { \
    _kvm_set_at(k, i, pkey, kb);                         \
    _kvm_set_at(v, i, pval, vb);                         \
} while (0)

#define _kvm_move_entry(dk, dv, i, sk, sv, j, kb, vb) do { \
    _kvm_move(dk, i, sk, j, kb);                           \
    _kvm_move(dv, i, sv, j, vb);                           \
} while (0)

const void* _kvm_get(const void* mv, const size_t c,
                     const size_t kb, const size_t vb,
                     const void* pkey) {
    const kvm_t* m = mv;
    const uint8_t* k = m->pk;
    const uint8_t* v = m->pv;
    const uint64_t key = _kvm_key(pkey, kb);
    const size_t h = _kvm_hash(key, c);
    size_t i = h; // start
    while (!_kvm_is_empty(m, i)) {
        if (_kvm_key_at(k, kb, i) == key) {
            return v + i * vb;
        } else {
            i = (i + 1) % c;
            if (i == h) { return 0; }
        }
    }
    return 0;
}

static bool _kvm_grow(kvm_t* m, const size_t kb, const size_t vb) {
    if (m->a >= (size_t)(UINTPTR_MAX / 2)) {
        kvm_fatal_return_zero("allocated overflow: %zd\n", m->a);
    }
    uint8_t*  k  = m->pk;
    uint8_t*  v  = m->pv;
    size_t    a  = m->a * 3 / 2;
    uint8_t*  pk = malloc(a * kb);
    uint8_t*  pv = malloc(a * vb);
    uint64_t* bm = calloc((a + 63) / 64, sizeof(uint64_t)); // zero init
    if (!pk || !pv || !bm) {
        free(pk); free(pv); free(bm);
        kvm_fatal_return_zero("out of memory\n");
    } else {
        // rehash all entries into new arrays:
        for (size_t i = 0; i < m->a; i++) {
            if (!_kvm_is_empty(m, i)) {
                uint64_t key = _kvm_key_at(k, kb, i);
                size_t h = _kvm_hash(key, a);
                while (!_kvm_bm_is_empty(bm, h)) {
                    h = (h + 1) % a;  // new kv map cannot be full
                }
                _kvm_move_entry(pk, pv, h, k, v, i, kb, vb);
                _kvm_bm_incl(bm, h);
            }
        }
        _kvm_set_pointers(m, pk, pv, bm);
        m->a = a;
        return true;
    }
}

bool _kvm_put(void* mv, const size_t capacity,
              const size_t kb, const size_t vb,
              const void* pkey, const void* pval) {
    kvm_t* m = mv;
    size_t c = capacity;
    if (m->a != 0) {
        const size_t c34 = c * 3 / 4;
        if (m->n >= c34) {
            if (!_kvm_grow(m, kb, vb)) { return false; }
            c = m->a;
        }
    }
    uint8_t* k = m->pk;
    uint8_t* v = m->pv;
    uint64_t key = _kvm_key(pkey, kb);
    size_t h = _kvm_hash(key, c);
    size_t i = h;
    while (!_kvm_is_empty(m, i)) {
        if (key == _kvm_key_at(k, kb, i)) {
            _kvm_set_entry(k, v, i, pkey, pval, kb, vb);
            return true;
        } else {
            i = (i + 1) % c;
            if (i == h) { kvm_fatal_return_zero("map is full\n"); }
        }
    }
    _kvm_set_entry(k, v, i, pkey, pval, kb, vb);
    _kvm_bm_incl(m->bm, i);
    m->n++;
    return true;
}

bool _kvm_delete(void* mv, const size_t c,
                 size_t kb, size_t vb, const void* pkey) {
    kvm_t* m = mv;
    uint8_t* k = m->pk;
    uint8_t* v = m->pv;
    const uint64_t key = _kvm_key(pkey, kb);
    size_t h = _kvm_hash(key, c);
    bool found = false;
    size_t i = h; // start
    while (!found && !_kvm_is_empty(m, i)) {
        const uint64_t ki = _kvm_key_at(k, kb, i);
        found = ki == key;
        if (!found) {
            i = (i + 1) % c;
            if (i == h) { break; }
        }
    }
    if (found) {
        _kvm_bm_excl(m->bm, i);
        size_t x = i;
        for (;;) {
            x = (x + 1) % c;
            if (_kvm_is_empty(m, x)) { break; }
            assert(x != i); // because empty slot exists
            const uint64_t kx = _kvm_key_at(k, kb, x);
            h = _kvm_hash(kx, c);
            const bool can_move = i <= x ? x < h || h <= i :
                                           x < h && h <= i;
            if (can_move) {
                _kvm_move_entry(k, v, i, k, v, x, kb, vb);
                _kvm_bm_incl(m->bm, i);
                _kvm_bm_excl(m->bm, x);
                i = x;
            }
        }
        m->n--;
    }
    return found;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif // kvm_implementation
