#ifndef kvmi_h_included
#define kvmi_h_included
/*
    # Usage:

    kvmi

    kvmi_fatalist = true; // errors will raise SIGABRT before returning false

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

    #define KVMI_VERIFY // implements expensive kvmi_verify for testing
    #include "kvmi.h"
*/

#include <signal.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

bool kvmi_fatalist; // any of kvm errors are fatal

#define kvmi_fatal(...) do {                            \
    if (kvmi_fatalist) {                                \
        fprintf(stderr, "" __VA_ARGS__);                \
        raise(SIGABRT);                                 \
    }                                                   \
} while (0)

struct _kvmi_list {
    struct _kvmi_list* prev;
    struct _kvmi_list* next;
};

struct kvmi_iterator {
    struct _kvmi_list* next;
    void* m; /* map */
    uint64_t mc;  /* modification count */
};

#define kvmi_fixed(tk, tv, _n_)                         \
    struct {                                            \
        uint8_t* pv;                                    \
        uint8_t* pk;                                    \
        uint64_t* bm;                                   \
        struct _kvmi_list* pn;  /* .prev .next list */  \
        size_t a;  /* allocated capacity */             \
        size_t n;  /* number of not empty entries */    \
        struct _kvmi_list*  head;                       \
        uint64_t mc;  /* modification count */          \
        /* fixed map: */                                \
        uint64_t bitmap[(((_n_ + 7) / 8)|1)];           \
        tv v[(_n_ + (_n_ == 0))];                       \
        tk k[(_n_ + (_n_ == 0))];                       \
        struct _kvmi_list list[(_n_ + (_n_ == 0))];     \
    }


#define kvmi_heap(tk, tv) kvmi_fixed(tk, tv, 0)

static bool _kvmi_init(void* mv, void* k, void* v, void* list, size_t n) {
    if (n < 4) {
        kvmi_fatal("invalid argument n: %zd minimum 4\n", n);
        return false;
    } else {
        kvmi_heap(void*, void*)* m = mv;
        memset(m->bitmap, 0, sizeof(m->bitmap));
        m->a  = 0;
        m->n  = 0;
        m->pk = k;
        m->pv = v;
        m->pn = list;
        m->bm = m->bitmap;
        m->head = 0;
        m->mc = 0;
        return true;
    }
}

// `kb` key bytes sizeof(tk) key type
// `vb` val bytes sizeof(tv) val type

static bool _kvmi_alloc(void* mv,  size_t kb, size_t vb, size_t n) {
    kvmi_heap(void*, void*)* m = mv;
    if (n >= 4) { // dynamically allocated map
        m->pk = malloc(n * kb);
        m->pv = malloc(n * vb);
        m->bm = calloc((n + 63) / 64, sizeof(uint64_t)); // zero init
        m->pn = malloc(n * sizeof(m->pn[0]));
        if (!m->pk || !m->pv || !m->bm || !m->pn) {
            free(m->pk); free(m->pv); free(m->bm); free(m->pn);
            kvmi_fatal("out of memory\n");
            return false;
        }
        m->a = n;
        m->n = 0;
        m->head = 0;
        m->mc = 0;
        return true;
    } else { // invalid usage
        kvmi_fatal("invalid argument n: %zd minimum 4\n", n);
        return false;
    }
}

static void _kvmi_set(void* mv, void* pk, void* pv, void* bm, void* pn) {
    kvmi_heap(void*, void*)* m = mv;
    free(m->pk); m->pk = pk;
    free(m->pv); m->pv = pv;
    free(m->bm); m->bm = bm;
    free(m->pn); m->pn = pn;
}

static void _kvmi_free(void* mv, size_t n) {
    kvmi_heap(void*, void*)* m = mv;
    if (n == 1 && m->a != 0) { _kvmi_set(mv, 0, 0, 0, 0); m->a = 0; }
}

static inline size_t _kvmi_hash(uint64_t key, size_t n) {
    key ^= key >> 33;
    key *= 0XFF51AFD7ED558CCDuLL;
    key ^= key >> 33;
    key *= 0XC4CEB9FE1A85EC53uLL;
    key ^= key >> 33;
    return (size_t)(key % n);
}

#define _kvmi_is_empty(m, i) (((m)->bm[(i) / 64] & (1uLL << ((i) % 64))) == 0)

static inline uint64_t _kvmi_key(const uint8_t* pkey, const size_t kb) {
    // if compiler propagates constant values of kb to this point
    // it can eliminate sequential ifs and expensive memcpy call
    if (kb == 1) { return *pkey; }
    if (kb == 2) { return *(uint16_t*)pkey; }
    if (kb == 4) { return *(uint32_t*)pkey; }
    if (kb == 8) { return *(uint64_t*)pkey; }
    uint64_t key = 0; memcpy(&key, pkey, kb); return key;
}

static inline uint64_t _kvmi_key_at(const uint8_t* k, const size_t kb,
                                   const size_t i) {
    return _kvmi_key(k + i * kb, kb);
}

static inline void _kvmi_set_at(uint8_t* d, const size_t i,
                              const uint8_t* s, const size_t b) {
    // if compiler propagates constant values of kb to this point
    // it can eliminate sequential ifs and expensive memcpy call
    if (b == 1) { d[i] = *s; return; }
    if (b == 2) { *(uint16_t*)(d + i * b) = *(uint16_t*)s; return; }
    if (b == 4) { *(uint32_t*)(d + i * b) = *(uint32_t*)s; return; }
    if (b == 8) { *(uint64_t*)(d + i * b) = *(uint64_t*)s; return; }
    memcpy(d + i * b, s, b);
}

static inline void _kvmi_move(uint8_t* d, const size_t i,
                            const uint8_t* s, const size_t j, const size_t b) {
    _kvmi_set_at(d, i, s + j * b, b);
}

const void* _kvmi_get(const void* mv, const size_t n,
                     const size_t kb, const size_t vb, const void* pkey) {
    const kvmi_heap(void*, void*)* m = mv;
    const uint8_t* k = (const uint8_t*)m->pk;
    const uint8_t* v = (const uint8_t*)m->pv;
    const uint64_t key = _kvmi_key(pkey, kb);
    const size_t h = _kvmi_hash(key, n);
    size_t i = h; // start
    while (!_kvmi_is_empty(m, i)) {
        if (_kvmi_key_at(k, kb, i) == key) {
            return v + i * vb;
        } else {
            i = (i + 1) % n;
            if (i == h) { return 0; }
        }
    }
    return 0;
}

static void _kvmi_link(struct _kvmi_list** head,
                      struct _kvmi_list pn[], size_t i) {
    if (!(*head)) {
        (*head) = pn[i].next = pn[i].prev = pn + i;
    } else {
        pn[i].next = (*head);
        pn[i].prev = (*head)->prev;
        (*head)->prev->next = pn + i;
        (*head)->prev = pn + i;
    }
}

static void _kvmi_unlink(struct _kvmi_list** head,
                        struct _kvmi_list pn[], size_t i) {
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

static bool _kvmi_find(void* mv, size_t i) {
    kvmi_heap(void*, void*)* m = mv;
    struct _kvmi_list* node = m->head;
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

static void _kvmi_verify(void* mv, size_t n) {
    #ifdef KVMI_VERIFY
    kvmi_heap(void*, void*)* m = mv;
    size_t count = 0;
    struct _kvmi_list* node = m->head;
    if (node) {
        do { count++; node = node->next; } while (node != m->head);
    }
    assert(count == m->n);
    node = m->head;
    if (node) {
        do {
            size_t i = node - m->pn;
            assert(i < n);
            assert(!_kvmi_is_empty(m, i));
            node = node->next;
        } while (node != m->head);
    }
    for (size_t i = 0; i < n; i++) {
        const bool empty = _kvmi_is_empty(m, i);
        const bool found = _kvmi_find(m, i);
        assert(empty == !found);
    }
    #else
    (void)mv; (void)n;
    #endif
}

static bool _kvmi_grow(void* mv, const size_t kb, const size_t vb) {
    kvmi_heap(void*, void*)* m = mv;
    if (m->a >= (size_t)(UINTPTR_MAX / 2)) {
        kvmi_fatal("allocated overflow: %zd\n", m->a);
        return false;
    }
    uint8_t*  k = (uint8_t*)m->pk;
    uint8_t*  v = (uint8_t*)m->pv;
    size_t    a  = m->a * 3 / 2;
    uint8_t*  pk = malloc(a * kb);
    uint8_t*  pv = malloc(a * vb);
    uint64_t* bm = calloc((a + 63) / 64, sizeof(uint64_t)); // zero init
    struct _kvmi_list* pn = malloc(a * sizeof(m->pn[0]));
    if (!pk || !pv || !bm || !pn) {
        free(pk); free(pv); free(bm); free(pn);
        kvmi_fatal("out of memory\n");
        return false;
    } else {
        struct _kvmi_list* head = 0; // new head
        struct _kvmi_list* node = m->head;
        // rehash all entries into new arrays:
        do {
            size_t i = node - m->pn;
            uint64_t key = _kvmi_key_at(k, kb, i);
            size_t h = _kvmi_hash(key, a);
            while ((bm[h / 64] & (1uLL << (h % 64))) != 0) {
                h = (h + 1) % a;  // new kv map cannot be full
            }
            _kvmi_move(pk, h, k, i, kb);
            _kvmi_move(pv, h, v, i, vb);
            bm[h / 64] |= (1uLL << (h % 64));
            _kvmi_link(&head, pn, h);
            node = node->next;
        } while (node != m->head);
        m->head = head;
        _kvmi_set(mv, pk, pv, bm, pn);
        m->a = a;
        return true;
    }
}

bool _kvmi_put(void* mv, const size_t capacity,
              const size_t kb, const size_t vb,
              const void* pkey, const void* pval) {
    kvmi_heap(void*, void*)* m = mv;
    size_t n = capacity;
    if (m->a != 0) {
        const size_t n34 = n * 3 / 4;
        if (m->n >= n34) {
            if (!_kvmi_grow(mv, kb, vb)) { return false; }
            n = m->a;
        }
    }
    uint8_t* k = (uint8_t*)m->pk;
    uint8_t* v = (uint8_t*)m->pv;
    uint64_t key = _kvmi_key(pkey, kb);
    size_t h = _kvmi_hash(key, n);
    size_t i = h;
    while (!_kvmi_is_empty(m, i)) {
        if (key == _kvmi_key_at(k, kb, i)) {
            _kvmi_set_at(k, i, pkey, kb);
            _kvmi_set_at(v, i, pval, vb);
            m->mc++; // TODO: this actually does not affect iterator. Is it necessary?
            return true;
        } else {
            i = (i + 1) % n;
            if (i == h) {
                kvmi_fatal("map is full\n");
                return false;
            }
        }
    }
    _kvmi_set_at(k, i, pkey, kb);
    _kvmi_set_at(v, i, pval, vb);
    _kvmi_link(&m->head, m->pn, i);
    m->bm[i / 64] |= (1uLL << (i % 64));
    m->n++;
    m->mc++;
    return true;
}

bool _kvmi_delete(void* mv, const size_t n, size_t kb, size_t vb,
                 const void* pkey) {
    kvmi_heap(void*, void*)* m = mv;
    uint8_t* k = (uint8_t*)m->pk;
    uint8_t* v = (uint8_t*)m->pv;
    const uint64_t key = _kvmi_key(pkey, kb);
    size_t h = _kvmi_hash(key, n);
    bool found = false;
    size_t i = h; // start
    while (!found && !_kvmi_is_empty(m, i)) {
        const uint64_t ki = _kvmi_key_at(k, kb, i);
        found = ki == key;
        if (!found) {
            i = (i + 1) % n;
            if (i == h) { break; }
        }
    }
    if (found) {
        m->bm[i / 64] &= ~(1uLL << (i % 64));
        _kvmi_unlink(&m->head, m->pn, i);
        size_t x = i;
        for (;;) {
            x = (x + 1) % n;
            if (_kvmi_is_empty(m, x)) { break; }
            assert(x != i); // because empty slot exists
            const uint64_t kx = _kvmi_key_at(k, kb, x);
            h = _kvmi_hash(kx, n);
            const bool can_move = i <= x ? x < h || h <= i :
                                           x < h && h <= i;
            if (can_move) {
                _kvmi_move(k, i, k, x, kb);
                _kvmi_move(v, i, v, x, vb);
                m->bm[i / 64] |=  (1uLL << (i % 64));
                m->bm[x / 64] &= ~(1uLL << (x % 64));
                _kvmi_unlink(&m->head, m->pn, x);
                _kvmi_link(&m->head, m->pn, i);
                i = x;
            }
        }
        m->mc++;
        m->n--;
    }
    return found;
}

struct kvmi_iterator kvmi_iterator(void* mv) {
    kvmi_heap(void*, void*)* m = mv;
    struct kvmi_iterator iterator = { .next = m->head, .m = mv, .mc = m->mc };
    return iterator;
}

void* _kvmi_next(struct kvmi_iterator* iterator, size_t kb, size_t vb, void* pval) {
    kvmi_heap(void*, void*)* m = iterator->m;
    if (m->mc != iterator->mc) {
        kvmi_fatal("map modified during iteration\n");
        return 0;
    } else {
        struct _kvmi_list* node = iterator->next;
        if (node) {
            iterator->next = node->next != m->head ? node->next : 0;
            const size_t i = node - m->pn;
            if (pval) { memcpy(pval, m->pv + i * vb, vb); }
            return m->pk + i * kb;
        } else {
            return 0;
        }
    }
}

bool kvmi_has_next(struct kvmi_iterator* iterator) {
    kvmi_heap(void*, void*)* m = iterator->m;
    if (m->mc != iterator->mc) {
        kvmi_fatal("map modified during iteration\n");
        return false;
    }
    return m->mc == iterator->mc && iterator->next != 0;
}

static void _kvmi_print(void* mv, size_t n, size_t kb, size_t vb) {
    kvmi_heap(void*, void*)* m = mv;
    if (m->head) {
        printf("head: %zd capacity: %zd entries: %zd\n",
               m->head - m->pn, n, m->n);
    } else {
        printf("head: null capacity: zd entries: %zd\n", n, m->n);
    }
    for (size_t i = 0; i < n; i++) {
        if (!_kvmi_is_empty(m, i)) {
            uint64_t key = _kvmi_key_at(m->pk, kb, i);
            size_t prev = m->pn[i].prev - m->pn;
            size_t next = m->pn[i].next - m->pn;
            printf("[%3zd] k=%016llX .prev=%3zd .next=%3zd ", i, key, prev, next);
            for (size_t k = 0; k < vb; k++) { printf("%02X", m->pv[i * vb + k]); }
            printf("\n");
        }
    }
}

#define kvmi_tk(m) typeof((m)->k[0]) // type of key
#define kvmi_tv(m) typeof((m)->v[0]) // type of val

#define kvmi_ka(m, key) (&(kvmi_tk(m)){(key)}) // key address
#define kvmi_va(m, val) (&(kvmi_tv(m)){(val)}) // val address

#define kvmi_kb(m) sizeof((m)->k[0]) // number of bytes in key
#define kvmi_vb(m) sizeof((m)->v[0]) // number of bytes in val

#define kvmi_fixed_n(m) (sizeof((m)->k) / kvmi_kb(m))

#define kvmi_capacity(m) ((m)->a > 0 ? (m)->a : kvmi_fixed_n(m))

#define kvmi_init(m)  _kvmi_init(m, &(m)->k, &(m)->v, &(m)->list, \
                                 kvmi_fixed_n(m))

#define kvmi_alloc(m, n) _kvmi_alloc(m, kvmi_kb(m), kvmi_vb(m), n)

#define kvmi_free(m) _kvmi_free(m, kvmi_fixed_n(m))

#define kvmi_put(m, key, val) _kvmi_put(m, kvmi_capacity(m), \
    kvmi_kb(m), kvmi_vb(m), kvmi_ka(m, key), kvmi_va(m, val))

#define kvmi_get(m, key) (kvmi_tv(m)*)_kvmi_get(m, kvmi_capacity(m), \
    kvmi_kb(m), kvmi_vb(m), kvmi_ka(m, key))

#define kvmi_delete(m, key) _kvmi_delete(m, kvmi_capacity(m), \
    kvmi_kb(m), kvmi_vb(m), kvmi_ka(m, key))

#define kvmi_verify(m) _kvmi_verify(m, kvmi_capacity(m))

#define kvmi_print(m) _kvmi_print(m, kvmi_capacity(m), kvmi_kb(m), kvmi_vb(m))

#define kvmi_next(m, iterator) \
        (kvmi_tk(m)*)_kvmi_next(iterator, kvmi_kb(m), kvmi_vb(m), 0)

#define kvmi_next_entry(m, iterator, pv) \
        (kvmi_tk(m)*)_kvmi_next(iterator, kvmi_kb(m), kvmi_vb(m), pv)

#endif // kvmi_h_included
