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

    #define KVMI_VERIFY // implements expensive kvm_verify for testing
    #include "kvmi.h"
*/

#include <signal.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

bool kvm_fatalist; // any of kvm errors are fatal

#define kvm_fatal(...) do {                             \
    if (kvm_fatalist) {                                 \
        fprintf(stderr, "" __VA_ARGS__);                \
        raise(SIGABRT);                                 \
    }                                                   \
} while (0)

struct _kvm_list {
    struct _kvm_list* prev;
    struct _kvm_list* next;
};

struct kvm_iterator {
    struct _kvm_list* next;
    void* m; /* map */
    uint64_t mc;  /* modification count */
};

#define kvm_fixed(tk, tv, _n_)                          \
    struct {                                            \
        uint8_t* pv;                                    \
        uint8_t* pk;                                    \
        uint64_t* bm;                                   \
        struct _kvm_list* pn;  /* .prev .next list */   \
        size_t a;  /* allocated capacity */             \
        size_t n;  /* number of not empty entries */    \
        struct _kvm_list*  head;                        \
        uint64_t mc;  /* modification count */          \
        /* fixed map: */                                \
        uint64_t bitmap[(((_n_ + 7) / 8)|1)];           \
        tv v[(_n_ + (_n_ == 0))];                       \
        tk k[(_n_ + (_n_ == 0))];                       \
        struct _kvm_list list[(_n_ + (_n_ == 0))];      \
    }


#define kvm_heap(tk, tv) kvm_fixed(tk, tv, 0)

static bool _kvm_init(void* mv, void* k, void* v, void* list, size_t n) {
    kvm_heap(void*, void*)* m = mv;
    memset(m->bitmap, 0, sizeof(m->bitmap));
    m->a  = 0;
    m->n  = 0;
    m->pk = k;
    m->pv = v;
    m->pn = list;
    m->bm = m->bitmap;
    m->head = 0;
    m->mc = 0;
    return n > 1;
}

// `kb` key bytes sizeof(tk) key type
// `vb` val bytes sizeof(tv) val type

static bool _kvm_alloc(void* mv,  size_t kb, size_t vb, size_t n) {
    kvm_heap(void*, void*)* m = mv;
    if (n > 0) { // dynamically allocated map
        m->pk = malloc(n * kb);
        m->pv = malloc(n * vb);
        m->bm = calloc((n + 63) / 64, sizeof(uint64_t)); // zero init
        m->pn = malloc(n * sizeof(m->pn[0]));
        if (!m->pk || !m->pv || !m->bm || !m->pn) {
            free(m->pk); free(m->pv); free(m->bm); free(m->pn);
            kvm_fatal("out of memory\n");
            return false;
        }
        m->a = n;
        m->n = 0;
        m->head = 0;
        m->mc = 0;
        return true;
    } else { // invalid usage
        kvm_fatal("invalid argument n: %zd\n", n);
        return false;
    }
}

static void _kvm_set(void* mv, void* pk, void* pv, void* bm, void* pn) {
    kvm_heap(void*, void*)* m = mv;
    free(m->pk); m->pk = pk;
    free(m->pv); m->pv = pv;
    free(m->bm); m->bm = bm;
    free(m->pn); m->pn = pn;
}

static void _kvm_free(void* mv, size_t n) {
    kvm_heap(void*, void*)* m = mv;
    if (n == 1 && m->a != 0) { _kvm_set(mv, 0, 0, 0, 0); m->a = 0; }
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
                     const size_t kb, const size_t vb, const void* pkey) {
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

static void _kvm_link(struct _kvm_list** head,
                      struct _kvm_list pn[], size_t i) {
    if (!(*head)) {
        (*head) = pn[i].next = pn[i].prev = pn + i;
    } else {
        pn[i].next = (*head);
        pn[i].prev = (*head)->prev;
        (*head)->prev->next = pn + i;
        (*head)->prev = pn + i;
    }
}

static void _kvm_unlink(struct _kvm_list** head,
                        struct _kvm_list pn[], size_t i) {
    if (*head == pn + i) {
        if ((*head)->next == (*head)) {
            (*head) = 0;
        } else {
            (*head) = pn[i].next;
        }
    }
    pn[i].next->prev = pn[i].prev;
    pn[i].prev->next = pn[i].next;
}

#ifdef KVMI_VERIFY

static bool _kvm_find(void* mv, size_t i) {
    kvm_heap(void*, void*)* m = mv;
    struct _kvm_list* node = m->head;
    if (node) {
        do {
            size_t j = node - m->pn;
            if (j == i) { return true; }
            node = node->next;
        } while (node != m->head);
    }
    return false;
}

#endif

static void _kvm_verify(void* mv, size_t n) {
    #ifdef KVMI_VERIFY
    kvm_heap(void*, void*)* m = mv;
    size_t count = 0;
    struct _kvm_list* node = m->head;
    if (node) {
        do { count++; node = node->next; } while (node != m->head);
    }
    assert(count == m->n);
    node = m->head;
    if (node) {
        do {
            size_t i = node - m->pn;
            assert(i < n);
            assert(!_kvm_is_empty(m, i));
            node = node->next;
        } while (node != m->head);
    }
    for (size_t i = 0; i < n; i++) {
        const bool empty = _kvm_is_empty(m, i);
        const bool found = _kvm_find(m, i);
        assert(empty == !found);
    }
    #else
    (void)mv; (void)n;
    #endif
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
    struct _kvm_list* pn = malloc(a * sizeof(m->pn[0]));
    if (!pk || !pv || !bm || !pn) {
        free(pk); free(pv); free(bm); free(pn);
        kvm_fatal("out of memory\n");
        return false;
    } else {
        struct _kvm_list* head = 0; // new head
        struct _kvm_list* node = m->head;
        // rehash all entries into new arrays:
        do {
            size_t i = node - m->pn;
            uint64_t key = _kvm_key_at(k, kb, i);
            size_t h = _kvm_hash(key, a);
            while ((bm[h / 64] & (1uLL << (h % 64))) != 0) {
                h = (h + 1) % a;  // new kv map cannot be full
            }
            _kvm_move(pk, h, k, i, kb);
            _kvm_move(pv, h, v, i, vb);
            bm[h / 64] |= (1uLL << (h % 64));
            _kvm_link(&head, pn, h);
            node = node->next;
        } while (node != m->head);
        m->head = head;
        _kvm_set(mv, pk, pv, bm, pn);
        m->a = a;
        return true;
    }
}

bool _kvm_put(void* mv, const size_t capacity,
              const size_t kb, const size_t vb,
              const void* pkey, const void* pval) {
    kvm_heap(void*, void*)* m = mv;
    size_t n = capacity;
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
            m->mc++; // TODO: this actually does not affect iterator. Is it necessary?
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
    _kvm_link(&m->head, m->pn, i);
    m->bm[i / 64] |= (1uLL << (i % 64));
    m->n++;
    m->mc++;
    return true;
}

bool _kvm_delete(void* mv, const size_t n, size_t kb, size_t vb,
                 const void* pkey) {
    kvm_heap(void*, void*)* m = mv;
    const uint8_t* v = _kvm_get(mv, n, kb, vb, pkey);
    if (v) {
        size_t i = (v - (const uint8_t*)m->pv) / vb;
        size_t x = i;
        uint8_t* k = (uint8_t*)m->pk;
        uint8_t* v_cast = (uint8_t*)m->pv;
        m->bm[i / 64] &= ~(1uLL << (i % 64));
        _kvm_unlink(&m->head, m->pn, i);
        for (;;) {
            x = (x + 1) % n;
            if (_kvm_is_empty(m, x)) { break; }
            size_t h = _kvm_hash(_kvm_key_at(k, kb, x), n);
            // Check if `h` lies within [i, x), accounting for wrap-around:
            if ((x < i) ^ (h <= i) ^ (h > x)) {
                _kvm_move(k, i, k, x, kb);
                _kvm_move(v_cast, i, v_cast, x, vb);
                m->bm[i / 64] |=  (1uLL << (i % 64));
                m->bm[x / 64] &= ~(1uLL << (x % 64));
                _kvm_unlink(&m->head, m->pn, x);
                _kvm_link(&m->head, m->pn, i);
                i = x;
            }
        }
        m->mc++;
        m->n--;
    }
    return v;
}

struct kvm_iterator kvm_iterator(void* mv) {
    kvm_heap(void*, void*)* m = mv;
    struct kvm_iterator iterator = { .next = m->head, .m = mv, .mc = m->mc };
    return iterator;
}

void* _kvm_next(struct kvm_iterator* iterator, size_t kb) {
    kvm_heap(void*, void*)* m = iterator->m;
    if (m->mc != iterator->mc) {
        kvm_fatal("map modified during iteration\n");
        return 0;
    } else {
        struct _kvm_list* node = iterator->next;
        if (node) {
            iterator->next = node->next != m->head ? node->next : 0;
            return m->pk + (node - m->pn) * kb;
        } else {
            return 0;
        }
    }
}

bool kvm_has_next(struct kvm_iterator* iterator) {
    kvm_heap(void*, void*)* m = iterator->m;
    if (m->mc != iterator->mc) {
        kvm_fatal("map modified during iteration\n");
        return false;
    }
    return m->mc == iterator->mc && iterator->next != 0;
}

static void _kvm_print(void* mv, size_t n, size_t kb, size_t vb) {
    kvm_heap(void*, void*)* m = mv;
    if (m->head) {
        printf("head: %zd capacity: %zd entries: %zd\n",
               m->head - m->pn, n, m->n);
    } else {
        printf("head: null capacity: zd entries: %zd\n", n, m->n);
    }
    for (size_t i = 0; i < n; i++) {
        if (!_kvm_is_empty(m, i)) {
            uint64_t key = _kvm_key_at(m->pk, kb, i);
            size_t prev = m->pn[i].prev - m->pn;
            size_t next = m->pn[i].next - m->pn;
            printf("[%3zd] k=%016llX .prev=%3zd .next=%3zd ", i, key, prev, next);
            for (size_t k = 0; k < vb; k++) { printf("%02X", m->pv[i * vb + k]); }
            printf("\n");
        }
    }
}

#define kvm_tk(m) typeof((m)->k[0]) // type of key
#define kvm_tv(m) typeof((m)->v[0]) // type of val

#define kvm_ka(m, key) (&(kvm_tk(m)){(key)}) // key address
#define kvm_va(m, val) (&(kvm_tv(m)){(val)}) // val address

#define kvm_kb(m) sizeof((m)->k[0]) // number of bytes in key
#define kvm_vb(m) sizeof((m)->v[0]) // number of bytes in val

#define kvm_fixed_n(m) (sizeof((m)->k) / kvm_kb(m))

#define kvm_capacity(m) ((m)->a > 0 ? (m)->a : kvm_fixed_n(m))

#define kvm_init(m)  _kvm_init((void*)m, &(m)->k, &(m)->v, &(m)->list, \
                               kvm_fixed_n(m))

#define kvm_alloc(m, n) _kvm_alloc((void*)m, kvm_kb(m), kvm_vb(m), n)

#define kvm_free(m) _kvm_free((void*)m, kvm_fixed_n(m))

#define kvm_put(m, key, val) _kvm_put(m, kvm_capacity(m), \
    kvm_kb(m), kvm_vb(m), kvm_ka(m, key), kvm_va(m, val))

#define kvm_get(m, key) (kvm_tv(m)*)_kvm_get(m, kvm_capacity(m), \
    kvm_kb(m), kvm_vb(m), kvm_ka(m, key))

#define kvm_delete(m, key) _kvm_delete(m, kvm_capacity(m), \
    kvm_kb(m), kvm_vb(m), kvm_ka(m, key))

#define kvm_verify(m) _kvm_verify(m, kvm_capacity(m))
#define kvm_print(m) _kvm_print(m, kvm_capacity(m), kvm_kb(m), kvm_vb(m))
#define kvm_next(m, iterator) (kvm_tk(m)*)_kvm_next(iterator, kvm_kb(m))

#endif // kvm_h_included
