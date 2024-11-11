#ifndef map_h_included
#define map_h_included
/*
    # Usage:

    map_fatalist = true; // errors will raise SIGABRT before returning false

    const char* k[] = {"hello", "good bye"};
    const char* v[] = {"world", "universe"};
    map_fixed(const char*, const char*, 4) m;
    map_init(&m);
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        map_put(&m, k[i], v[i]);
        swear(*map_get(&m, k[i]) == v[i]);
    }
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

    #define map_VERIFY // implements expensive map_verify for testing
    #include "map.h"
*/

#include <signal.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

bool map_fatalist; // any of map errors are fatal

#define _map_fatal_return_zero(...) do { \
    if (map_fatalist) {                  \
        fprintf(stderr, "" __VA_ARGS__); \
        raise(SIGABRT);                  \
    }                                    \
    return 0; /* false of (void*)0 */    \
} while (0)

#define _map_oom "out of memory\n"

struct _map_list {
    struct _map_list* prev;
    struct _map_list* next;
};

struct map_iterator {
    struct _map_list* next;
    void* m; /* map */
    uint64_t mc;  /* modification count */
};

enum map_tag {
    map_tag_none   = 0,
    map_tag_fixed  = 1 << 0,
    map_tag_keydup = 1 << 1, // strdup() for keys
    map_tag_valdup = 1 << 2  // strdup() for values
};

#define map_struct(tk, tv, _n_)                          \
    struct {                                            \
        uint64_t tag;                                   \
        uint8_t* pv;                                    \
        uint8_t* pk;                                    \
        uint64_t* bm;                                   \
        struct _map_list* pn;  /* .prev .next list */   \
        size_t a;  /* allocated capacity */             \
        size_t n;  /* number of not empty entries */    \
        int    (*cmp)(uint64_t, uint64_t);              \
        size_t (*hash)(uint64_t, size_t n);             \
        struct _map_list*  head;                        \
        uint64_t mc;  /* modification count */          \
        /* fixed map: */                                \
        uint64_t bitmap[(((_n_ + 7) / 8)|1)];           \
        tv v[(_n_ + (_n_ == 0))];                       \
        tk k[(_n_ + (_n_ == 0))];                       \
        struct _map_list list[(_n_ + (_n_ == 0))];      \
    }

#define map_2_arg(tk, tv)    map_struct(tk, tv, 0)
#define map_3_arg(tk, tv, n) map_struct(tk, tv, n)
#define map_get_4th_arg(arg1, arg2, arg3, arg4, ...) arg4
#define map_chooser(...) map_get_4th_arg(__VA_ARGS__, map_3_arg, map_2_arg, )
#define map(...) map_chooser(__VA_ARGS__)(__VA_ARGS__)

static bool _map_init(void* mv, void* k, void* v, void* list, size_t n,
                      int (*cmp)(uint64_t, uint64_t),
                      size_t (*hash)(uint64_t, size_t),
                      enum map_tag tag) {
    if (n < 4) {
        _map_fatal_return_zero("invalid argument n: %zd minimum 4\n", n);
    } else {
        map(void*, void*)* m = mv;
        memset(m->bitmap, 0, sizeof(m->bitmap));
        m->tag  = tag;
        m->a    = 0;
        m->n    = 0;
        m->pk   = k;
        m->pv   = v;
        m->pn   = list;
        m->bm   = m->bitmap;
        m->head = 0;
        m->mc   = 0;
        m->cmp  = cmp;
        m->hash = hash;
        return true;
    }
}

// `kb` key bytes sizeof(tk) key type
// `vb` val bytes sizeof(tv) val type

static bool _map_alloc(void* mv, size_t kb, size_t vb, size_t n, size_t c,
                       int (*cmp)(uint64_t, uint64_t),
                       size_t (*hash)(uint64_t, size_t),
                       enum map_tag tag) {
    map(void*, void*)* m = mv;
    if (c == 1 && n >= 4) {
        m->tag = tag;
        m->pk  = malloc(n * kb);
        m->pv  = malloc(n * vb);
        m->bm  = calloc((n + 63) / 64, sizeof(uint64_t)); // zero init
        m->pn  = malloc(n * sizeof(m->pn[0]));
        if (!m->pk || !m->pv || !m->bm || !m->pn) {
            free(m->pk); free(m->pv); free(m->bm); free(m->pn);
            _map_fatal_return_zero(_map_oom);
        }
        m->a    = n;
        m->n    = 0;
        m->head = 0;
        m->mc   = 0;
        m->cmp  = cmp;
        m->hash = hash;
        return true;
    } else { // invalid usage
        _map_fatal_return_zero("invalid argument n: %zd minimum 4 \n", n);
    }
}

struct map_iterator map_iterator(void* mv) {
    map(void*, void*)* m = mv;
    struct map_iterator iterator = { .next = m->head, .m = mv, .mc = m->mc };
    return iterator;
}

void* _map_next(struct map_iterator* iterator, size_t kb, size_t vb, void* pval) {
    map(void*, void*)* m = iterator->m;
    if (m->mc != iterator->mc) {
        _map_fatal_return_zero("map modified during iteration\n");
    } else {
        struct _map_list* node = iterator->next;
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

bool map_has_next(struct map_iterator* iterator) {
    map(void*, void*)* m = iterator->m;
    if (m->mc != iterator->mc) {
        _map_fatal_return_zero("map modified during iteration\n");
    }
    return m->mc == iterator->mc && iterator->next != 0;
}

#define _map_undup_key(m, i, kb) do {           \
    if (m->tag & map_tag_keydup) {              \
        void** pki = (void**)(m->pk + i * kb);  \
        if (pki) { free(*pki); *pki = 0; }      \
    }                                           \
} while (0)

#define _map_undup_val(m, i, vb) do {           \
    if (m->tag & map_tag_valdup) {              \
        void** pvi = (void**)(m->pv + i * vb);  \
        if (pvi) { free(*pvi); *pvi = 0; }      \
    }                                           \
} while (0)

#define _map_undup(m, i, kb, vb) do {           \
    _map_undup_key(m, i, kb);                   \
    _map_undup_val(m, i, vb);                   \
} while (0)

static void _map_set(void* mv, void* pk, void* pv, void* bm, void* pn) {
    map(void*, void*)* m = mv;
    free(m->pk); m->pk = pk;
    free(m->pv); m->pv = pv;
    free(m->bm); m->bm = bm;
    free(m->pn); m->pn = pn;
}

static void _map_clear(void* mv, size_t c, size_t kb, size_t vb) {
    map(void*, void*)* m = mv;
    if (m->tag & (map_tag_keydup | map_tag_valdup)) {
        struct map_iterator iterator = map_iterator(mv);
        while (map_has_next(&iterator)) {
            const uint8_t* pkey = _map_next(&iterator, kb, vb, 0);
            size_t i = (pkey - (uint8_t*)m->pk) / kb;
            _map_undup(m, i, kb, vb);
        }
    }
    m->n = 0;
    const size_t n = m->a > 0 ? m->a : c;
    memset(m->bm, 0, ((n + 63) / 64) * sizeof(m->bm[0]));
}

static void _map_free(void* mv, size_t c, size_t kb, size_t vb) {
    _map_clear(mv, c, kb, vb);
    map(void*, void*)* m = mv;
    if (c == 1 && m->a != 0) { _map_set(mv, 0, 0, 0, 0); m->a = 0; }
}

static inline size_t _map_hash(uint64_t key, size_t n) {
    key ^= key >> 33;
    key *= 0XFF51AFD7ED558CCDuLL;
    key ^= key >> 33;
    key *= 0XC4CEB9FE1A85EC53uLL;
    key ^= key >> 33;
    return (size_t)(key % n);
}

#define map_hash(m, k, n) ((m)->hash ? (m)->hash(k, n) : _map_hash(k, n))

static inline uint64_t _map_hash_str(uint64_t key, size_t n) {
    const char* s =(const char*)(uintptr_t)key;
    uint64_t h = 0xcbf29ce484222325uLL; // FNV-1a 64-bit offset basis
    if (s) { // map_str(const char*, const char*) allow null keys and values
        while (*s) {
            h ^= (uint8_t)(*s++);
            h *= 0x100000001b3uLL; // FNV-1a 64-bit prime
        }
    }
    return (size_t)(h % n);
}

static inline int _map_strcmp(uint64_t k0, uint64_t k1) {
    return k0 == k1 ? 0 :
        strcmp((const char*)(uintptr_t)k0, (const char*)(uintptr_t)k1);
}

#define _map_is_empty(m, i) (((m)->bm[(i) / 64] & (1uLL << ((i) % 64))) == 0)

static inline uint64_t _map_key(const uint8_t* pkey, const size_t kb) {
    // if compiler propagates constant values of kb to this point
    // it can eliminate sequential ifs and expensive memcpy call
    if (kb == 1) { return *pkey; }
    if (kb == 2) { return *(uint16_t*)pkey; }
    if (kb == 4) { return *(uint32_t*)pkey; }
    if (kb == 8) { return *(uint64_t*)pkey; }
    uint64_t key = 0; memcpy(&key, pkey, kb); return key;
}

static inline uint64_t _map_key_at(const uint8_t* k, const size_t kb,
                                   const size_t i) {
    return _map_key(k + i * kb, kb);
}

static inline void _map_set_at(uint8_t* d, const size_t i,
                              const uint8_t* s, const size_t b) {
    // if compiler propagates constant values of kb to this point
    // it can eliminate sequential ifs and expensive memcpy call
    if (b == 1) { d[i] = *s; return; }
    if (b == 2) { *(uint16_t*)(d + i * b) = *(uint16_t*)s; return; }
    if (b == 4) { *(uint32_t*)(d + i * b) = *(uint32_t*)s; return; }
    if (b == 8) { *(uint64_t*)(d + i * b) = *(uint64_t*)s; return; }
    memcpy(d + i * b, s, b);
}

static inline void _map_move(uint8_t* d, const size_t i,
                             const uint8_t* s, const size_t j,
                             const size_t b) {
    _map_set_at(d, i, s + j * b, b);
}

const void* _map_get(const void* mv, const size_t n,
                     const size_t kb, const size_t vb, const void* pkey) {
    const map(void*, void*)* m = mv;
    const uint8_t* k = (const uint8_t*)m->pk;
    const uint8_t* v = (const uint8_t*)m->pv;
    const uint64_t key = _map_key(pkey, kb);
    const size_t h = map_hash(m, key, n);
    size_t i = h; // start
    while (!_map_is_empty(m, i)) {
        const uint64_t ki = _map_key_at(k, kb, i);
        if (m->cmp ? m->cmp(ki, key) == 0 : ki == key) {
            return v + i * vb;
        } else {
            i = (i + 1) % n;
            if (i == h) { return 0; }
        }
    }
    return 0;
}

static void _map_link(struct _map_list** head,
                      struct _map_list pn[], size_t i) {
    if (!(*head)) {
        (*head) = pn[i].next = pn[i].prev = pn + i;
    } else {
        pn[i].next = (*head);
        pn[i].prev = (*head)->prev;
        (*head)->prev->next = pn + i;
        (*head)->prev = pn + i;
    }
}

static void _map_unlink(struct _map_list** head,
                        struct _map_list pn[], size_t i) {
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

#ifdef map_VERIFY

static bool _map_find(void* mv, size_t i) {
    map(void*, void*)* m = mv;
    struct _map_list* node = m->head;
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

static void _map_verify(void* mv, size_t n) {
    #ifdef map_VERIFY
    map(void*, void*)* m = mv;
    size_t count = 0;
    struct _map_list* node = m->head;
    if (node) {
        do { count++; node = node->next; } while (node != m->head);
    }
    assert(count == m->n);
    node = m->head;
    if (node) {
        do {
            size_t i = node - m->pn;
            assert(i < n);
            assert(!_map_is_empty(m, i));
            node = node->next;
        } while (node != m->head);
    }
    for (size_t i = 0; i < n; i++) {
        const bool empty = _map_is_empty(m, i);
        const bool found = _map_find(m, i);
        assert(empty == !found);
    }
    #else
    (void)mv; (void)n;
    #endif
}

static bool _map_grow(void* mv, const size_t kb, const size_t vb) {
    map(void*, void*)* m = mv;
    if (m->a >= (size_t)(UINTPTR_MAX / 2)) {
        _map_fatal_return_zero("overflow: %zd\n", m->a);
    }
    uint8_t*  k = (uint8_t*)m->pk;
    uint8_t*  v = (uint8_t*)m->pv;
    size_t    a  = m->a * 3 / 2;
    uint8_t*  pk = malloc(a * kb);
    uint8_t*  pv = malloc(a * vb);
    uint64_t* bm = calloc((a + 63) / 64, sizeof(uint64_t)); // zero init
    struct _map_list* pn = malloc(a * sizeof(m->pn[0]));
    if (!pk || !pv || !bm || !pn) {
        free(pk); free(pv); free(bm); free(pn);
        _map_fatal_return_zero(_map_oom);
    } else {
        struct _map_list* head = 0; // new head
        struct _map_list* node = m->head;
        // rehash all entries into new arrays:
        do {
            size_t i = node - m->pn;
            uint64_t key = _map_key_at(k, kb, i);
            size_t h = map_hash(m, key, a);
            while ((bm[h / 64] & (1uLL << (h % 64))) != 0) {
                h = (h + 1) % a;  // new kv map cannot be full
            }
            _map_move(pk, h, k, i, kb);
            _map_move(pv, h, v, i, vb);
            bm[h / 64] |= (1uLL << (h % 64));
            _map_link(&head, pn, h);
            node = node->next;
        } while (node != m->head);
        m->head = head;
        _map_set(mv, pk, pv, bm, pn);
        m->a = a;
        return true;
    }
}

bool _map_put(void* mv, const size_t capacity,
              const size_t kb, const size_t vb,
              const void* pkey, const void* pval) {
    map(void*, void*)* m = mv;
    size_t n = capacity;
    if (m->a != 0) {
        const size_t n34 = n * 3 / 4;
        if (m->n >= n34) {
            if (!_map_grow(mv, kb, vb)) { return false; }
            n = m->a;
        }
    }
    uint64_t key;
    void* key_dup = 0;
    if (m->tag & map_tag_keydup) {
        if (*(void**)pkey) {
            key_dup = strdup(*(const char**)pkey);
            if (!key_dup) { _map_fatal_return_zero(_map_oom); }
        }
        pkey = &key_dup;
        key = (uintptr_t)key_dup;
    } else {
        key  = _map_key(pkey, kb);
    }
    void* val_dup = 0;
    if (m->tag & map_tag_valdup) {
        if (*(void**)pval) {
            val_dup = strdup(*(const char**)pval);
            if (!val_dup) { free(key_dup); _map_fatal_return_zero(_map_oom); }
        }
        pval = &val_dup;
    }
    uint8_t* k = (uint8_t*)m->pk;
    uint8_t* v = (uint8_t*)m->pv;
    const size_t h = map_hash(m, key, n);
    size_t i = h;
    while (!_map_is_empty(m, i)) {
        const uint64_t ki = _map_key_at(k, kb, i);
        if (m->cmp ? m->cmp(ki, key) == 0 : ki == key) {
            _map_undup_key(m, i, kb);
            _map_set_at(k, i, pkey, kb);
            _map_set_at(v, i, pval, vb);
            // m->mc is not incremented because key set is not changed
            return true;
        } else {
            i = (i + 1) % n;
            if (i == h) { _map_fatal_return_zero("map is full\n"); }
        }
    }
    _map_set_at(k, i, pkey, kb);
    _map_set_at(v, i, pval, vb);
    _map_link(&m->head, m->pn, i);
    m->bm[i / 64] |= (1uLL << (i % 64));
    m->n++;
    m->mc++;
    return true;
}

static inline bool map_in_between(size_t i, size_t x, size_t h) {
    return i <= x ? i < h && h <= x :
                    i < h || h <= x;
}

bool _map_delete(void* mv, const size_t n, size_t kb, size_t vb,
                 const void* pkey) {
    map(void*, void*)* m = mv;
    uint8_t* k = (uint8_t*)m->pk;
    uint8_t* v = (uint8_t*)m->pv;
    const uint64_t key = _map_key(pkey, kb);
    size_t h = m->hash ? m->hash(key, n) : _map_hash(key, n);
    bool found = false;
    size_t i = h; // start
    while (!found && !_map_is_empty(m, i)) {
        const uint64_t ki = _map_key_at(k, kb, i);
        found = m->cmp ? m->cmp(ki, key) == 0 : ki == key;
        if (!found) {
            i = (i + 1) % n;
            if (i == h) { break; }
        }
    }
    if (found) {
        m->bm[i / 64] &= ~(1uLL << (i % 64));
        _map_undup(m, i, kb, vb);
        _map_unlink(&m->head, m->pn, i);
        size_t x = i;
        for (;;) {
            x = (x + 1) % n;
            if (_map_is_empty(m, x)) { break; }
            assert(x != i); // because empty slot exists
            const uint64_t kx = _map_key_at(k, kb, x);
            h = m->hash ? m->hash(kx, n) : _map_hash(kx, n);
            const bool can_move = i <= x ? x < h || h <= i :
                                           x < h && h <= i;
            if (can_move) {
                _map_move(k, i, k, x, kb);
                _map_move(v, i, v, x, vb);
                m->bm[i / 64] |=  (1uLL << (i % 64));
                m->bm[x / 64] &= ~(1uLL << (x % 64));
                _map_unlink(&m->head, m->pn, x);
                _map_link(&m->head, m->pn, i);
                i = x;
            }
        }
        m->mc++;
        m->n--;
    }
    return found;
}

static void _map_print(void* mv, size_t n, size_t kb, size_t vb) {
    map(void*, void*)* m = mv;
    if (m->head) {
        printf("head: %zd capacity: %zd entries: %zd\n",
               m->head - m->pn, n, m->n);
    } else {
        printf("head: null capacity: zd entries: %zd\n", n, m->n);
    }
    for (size_t i = 0; i < n; i++) {
        if (!_map_is_empty(m, i)) {
            const uint64_t key = _map_key_at(m->pk, kb, i);
            const size_t prev = m->pn[i].prev - m->pn;
            const size_t next = m->pn[i].next - m->pn;
            printf("[%3zd] k=%016llX .prev=%3zd .next=%3zd ", i, key, prev, next);
            for (size_t k = 0; k < vb; k++) { printf("%02X", m->pv[i * vb + k]); }
            const uint64_t h = _map_hash(key, n);
            printf(" hash=%lld\n", h);
        }
    }
}

#define map_tk(m) typeof((m)->k[0]) // type of key
#define map_tv(m) typeof((m)->v[0]) // type of val

#define map_ka(m, key) (&(map_tk(m)){(key)}) // key address
#define map_va(m, val) (&(map_tv(m)){(val)}) // val address

#define map_kb(m) sizeof((m)->k[0]) // number of bytes in key
#define map_vb(m) sizeof((m)->v[0]) // number of bytes in val

#define map_fixed_c(m) (sizeof((m)->k) / map_kb(m))

#define map_capacity(m) ((m)->a > 0 ? (m)->a : map_fixed_c(m))

#define map_init(m) _Generic(((m)->k[0]),                                   \
     const char*:                                                           \
        _map_init((void*)m, &(m)->k, &(m)->v, &(m)->list, map_fixed_c(m),   \
                  _map_strcmp, _map_hash_str, map_tag_fixed),               \
     default:                                                               \
        _map_init((void*)m, &(m)->k, &(m)->v, &(m)->list, map_fixed_c(m),   \
                  0, 0, map_tag_fixed)                                      \
)

#define map_alloc(m, n) _Generic(((m)->k[0]),                               \
     const char*:                                                           \
        _map_alloc(m, map_kb(m), map_vb(m), n, map_fixed_c(m),              \
                  _map_strcmp, _map_hash_str, map_tag_none),                \
     default:                                                               \
        _map_alloc(m, map_kb(m), map_vb(m), n, map_fixed_c(m),              \
                   0, 0, map_tag_none)                                      \
)

#define map_str(m, n) _Generic(((m)->k[0]),                                 \
     const char*: _Generic(((m)->v[0]),                                     \
        const char*:                                                        \
            _map_alloc(m, map_kb(m), map_vb(m), n, map_fixed_c(m),          \
                       _map_strcmp, _map_hash_str,                          \
                       map_tag_keydup|map_tag_valdup),                      \
        default:                                                            \
            _map_alloc(m, map_kb(m), map_vb(m), n, map_fixed_c(m),          \
                  _map_strcmp, _map_hash_str, map_tag_keydup)               \
     ),                                                                     \
     default: _Generic(((m)->v[0]),                                         \
        const char*:                                                        \
            _map_alloc(m, map_kb(m), map_vb(m), n, map_fixed_c(m),          \
                       0, 0, map_tag_valdup),                               \
        default:                                                            \
            _map_alloc(m, map_kb(m), map_vb(m), n, map_fixed_c(m),          \
                       0, 0, map_tag_none)                                  \
     )                                                                      \
)

#define map_clear(m) _map_clear((void*)m, map_fixed_c(m), map_kb(m), map_vb(m))
#define map_free(m) _map_free((void*)m, map_fixed_c(m), map_kb(m), map_vb(m))

#define map_put(m, key, val) _map_put(m, map_capacity(m), \
    map_kb(m), map_vb(m), map_ka(m, key), map_va(m, val))

#define map_get(m, key) (map_tv(m)*)_map_get(m, map_capacity(m), \
    map_kb(m), map_vb(m), map_ka(m, key))

#define map_delete(m, key) _map_delete(m, map_capacity(m), \
    map_kb(m), map_vb(m), map_ka(m, key))

#define map_verify(m) _map_verify(m, map_capacity(m))

#define map_print(m) _map_print(m, map_capacity(m), map_kb(m), map_vb(m))

#define map_next(m, iterator) \
        (map_tk(m)*)_map_next(iterator, map_kb(m), map_vb(m), 0)

#define map_next_entry(m, iterator, pv) \
        (map_tk(m)*)_map_next(iterator, map_kb(m), map_vb(m), pv)

#endif // map_h_included
