#ifndef map_h_included
#define map_h_included
/*
    # Usage:

    map_fatalist = true; // errors will raise SIGABRT before returning false

    const char* k[] = {"hello", "good bye"};
    const char* v[] = {"world", "universe"};
    map(const char*, const char*, 4) m; // fixed size map
    map_alloc(&m);
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
    map_free(&m);
*/

#include <stdint.h>
#include <stdbool.h>

#include <signal.h>
#include <stdlib.h>
#include <string.h>

bool map_fatalist; // any of map errors are fatal

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
    map_heap   = 0,
    map_keydup = 1, // strdup() for keys
    map_valdup = 2, // strdup() for values
    map_strdup = 3  // strdup() for keys & values
};

#define map_struct(tk, tv, _n_, _tags_)                 \
    struct {                                            \
        uint64_t  tag;                                  \
        uint8_t*  pv;                                   \
        uint8_t*  pk;                                   \
        uint64_t* bm;                                   \
        struct _map_list* pn;  /* .prev .next list */   \
        size_t n;  /* number of not empty entries */    \
        size_t a;  /* allocated capacity */             \
        int    (*cmp)(uint64_t, uint64_t);              \
        size_t (*hash)(uint64_t, size_t n);             \
        struct _map_list*  head;                        \
        uint64_t mc;  /* modification count */          \
        union {                                         \
            uint64_t tags_aligned;                      \
            uint8_t  tags[(_tags_) + 1];                \
        };                                              \
        /* fixed map: */                                \
        uint64_t bitmap[(((_n_ + 7) / 8)|1)];           \
        tv v[(_n_ + (_n_ == 0))];                       \
        tk k[(_n_ + (_n_ == 0))];                       \
        struct _map_list list[(_n_ + (_n_ == 0))];      \
    }

#ifdef __cplusplus
extern "C" {
#endif

bool _map_init(void* mv, size_t tag, size_t kb, size_t vb, size_t n,
               void* k, void* v, void* list, size_t c,
               int (*cmp)(uint64_t, uint64_t),
               size_t (*hash)(uint64_t, size_t));

size_t _map_str_hash(uint64_t key, size_t c);

int _map_str_cmp(uint64_t k0, uint64_t k1);

const void* _map_get(const void* mv, const size_t c,
                     const size_t kb, const size_t vb, const void* pkey);

bool _map_put(void* mv, const size_t capacity, const size_t kb, const size_t vb,
              const void* pkey, const void* pval);

bool _map_delete(void* mv, const size_t c, size_t kb, size_t vb, const void* pkey);

struct map_iterator map_iterator(void* mv);

void* _map_next(struct map_iterator* iterator, size_t kb, size_t vb, void* pval);

bool map_has_next(struct map_iterator* iterator);

void _map_clear(void* mv, size_t c, size_t kb, size_t vb);

void _map_free(void* mv, size_t c, size_t kb, size_t vb);

#ifdef __cplusplus
} // extern "C"
#endif

#define _map_2_arg(tk, tv)           map_struct(tk, tv, 0, 0)
#define _map_3_arg(tk, tv, n)        map_struct(tk, tv, n, 0)
#define _map_4_arg(tk, tv, n, tags)  map_struct(tk, tv, n, tags)
#define _map_get_5th_arg(arg1, arg2, arg3, arg4, arg5, ...) arg5
#define _map_chooser(...) _map_get_5th_arg(__VA_ARGS__, \
                          _map_4_arg, _map_3_arg, _map_2_arg, )
#define map(...) _map_chooser(__VA_ARGS__)(__VA_ARGS__)

#define _map_tk(m) typeof((m)->k[0]) // type of key
#define _map_tv(m) typeof((m)->v[0]) // type of val

#define _map_ka(m, key) (&(_map_tk(m)){(key)}) // key address
#define _map_va(m, val) (&(_map_tv(m)){(val)}) // val address

#define _map_kb(m) sizeof((m)->k[0]) // number of bytes in key
#define _map_vb(m) sizeof((m)->v[0]) // number of bytes in val

#define _map_fixed_c(m) (sizeof((m)->k) / _map_kb(m))

#define map_capacity(m) ((m)->a > 0 ? (m)->a : _map_fixed_c(m))

#define map_init(m, n) _Generic(((m)->k[0]),                           \
     const char*:                                                      \
        _map_init(m, sizeof((m)->tags) - 1, _map_kb(m), _map_vb(m), n, \
                  &(m)->k, &(m)->v, &(m)->list, _map_fixed_c(m),       \
                  _map_str_cmp, _map_str_hash),                        \
     default:                                                          \
        _map_init(m, sizeof((m)->tags) - 1, _map_kb(m), _map_vb(m), n, \
                  &(m)->k, &(m)->v, &(m)->list, _map_fixed_c(m),       \
                  /*_map_str_cmp: */0, /*_map_str_hash: */ 0)          \
)

#define _map_init_1_arg(m)    map_init(m, 0)
#define _map_init_2_arg(m, n) map_init(m, n)
#define _map_get_3rd_arg(arg1, arg2, arg3, ...) arg3
#define _map_init_chooser(...) _map_get_3rd_arg(__VA_ARGS__, \
                               _map_init_2_arg, _map_init_1_arg, )
#define map_alloc(...) _map_init_chooser(__VA_ARGS__)(__VA_ARGS__)

#define map_clear(m) _map_clear(m, _map_fixed_c(m), _map_kb(m), _map_vb(m))
#define map_free(m)  _map_free(m,  _map_fixed_c(m), _map_kb(m), _map_vb(m))

#define map_put(m, key, val) _map_put(m, map_capacity(m), \
    _map_kb(m), _map_vb(m), _map_ka(m, key), _map_va(m, val))

#define map_get(m, key) (_map_tv(m)*)_map_get(m, map_capacity(m), \
    _map_kb(m), _map_vb(m), _map_ka(m, key))

#define map_delete(m, key) _map_delete(m, map_capacity(m), \
    _map_kb(m), _map_vb(m), _map_ka(m, key))

#define map_print(m) _map_print(m, map_capacity(m), _map_kb(m), _map_vb(m))

#define map_next(m, iterator) \
        (_map_tk(m)*)_map_next(iterator, _map_kb(m), _map_vb(m), 0)

#define map_next_entry(m, iterator, pv) \
        (_map_tk(m)*)_map_next(iterator, _map_kb(m), _map_vb(m), pv)

#endif // map_h_included

#if defined(map_implementation) && !defined(map_implemented)

#define map_implemented

#ifdef __cplusplus
extern "C" {
#endif

#define _map_fatal_return_zero(...) do { \
    if (map_fatalist) {                  \
        fprintf(stderr, "" __VA_ARGS__); \
        raise(SIGABRT);                  \
    }                                    \
    return 0; /* false of (void*)0 */    \
} while (0)

#define _map_oom "out of memory\n"

typedef map(void*, void*) map_t;

// `kb` key bytes sizeof(tk) key type
// `vb` val bytes sizeof(tv) val type

static bool _map_alloc(map_t* m, size_t kb, size_t vb, size_t n, size_t c,
                       int (*cmp)(uint64_t, uint64_t),
                       size_t (*hash)(uint64_t, size_t),
                       size_t tag) {
    if (c == 1 && n >= 4) {
        m->pk  = malloc(n * kb);
        m->pv  = malloc(n * vb);
        m->bm  = calloc((n + 63) / 64, sizeof(uint64_t)); // zero init
        m->pn  = malloc(n * sizeof(m->pn[0]));
        if (!m->pk || !m->pv || !m->bm || !m->pn) {
            free(m->pk); free(m->pv); free(m->bm); free(m->pn);
            _map_fatal_return_zero(_map_oom);
        }
        m->tag  = tag;
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

bool _map_init(void* mv, size_t tag, size_t kb, size_t vb, size_t n,
               void* k, void* v, void* list, size_t c,
               int (*cmp)(uint64_t, uint64_t),
               size_t (*hash)(uint64_t, size_t)) {
    map_t* m = mv;
    if (c == 1) {
        return _map_alloc(m, kb, vb, n, c, cmp, hash, tag);
    } else if (n != 0) {
        _map_fatal_return_zero("invalid argument n: %zd\n", n);
    } else {
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

struct map_iterator map_iterator(void* mv) {
    map_t* m = mv;
    struct map_iterator iterator = { .next = m->head, .m = mv, .mc = m->mc };
    return iterator;
}

void* _map_next(struct map_iterator* iterator, size_t kb, size_t vb, void* pval) {
    map_t* m = iterator->m;
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
    map_t* m = iterator->m;
    if (m->mc != iterator->mc) {
        _map_fatal_return_zero("map modified during iteration\n");
    }
    return m->mc == iterator->mc && iterator->next != 0;
}

#define _map_undup_key(m, i, kb) do {           \
    if (m->tag & map_keydup) {                  \
        void** pki = (void**)(m->pk + i * kb);  \
        if (pki) { free(*pki); *pki = 0; }      \
    }                                           \
} while (0)

#define _map_undup_val(m, i, vb) do {           \
    if (m->tag & map_valdup) {                  \
        void** pvi = (void**)(m->pv + i * vb);  \
        if (pvi) { free(*pvi); *pvi = 0; }      \
    }                                           \
} while (0)

#define _map_undup(m, i, kb, vb) do {           \
    _map_undup_key(m, i, kb);                   \
    _map_undup_val(m, i, vb);                   \
} while (0)

static void _map_set_pointers(map_t* m, void* pk, void* pv, void* bm, void* pn) {
    free(m->pk); m->pk = pk;
    free(m->pv); m->pv = pv;
    free(m->bm); m->bm = bm;
    free(m->pn); m->pn = pn;
}

void _map_clear(void* mv, size_t c, size_t kb, size_t vb) {
    map_t* m = mv;
    if (m->tag & (map_keydup | map_valdup)) {
        struct map_iterator iterator = map_iterator(mv);
        while (map_has_next(&iterator)) {
            const uint8_t* pkey = _map_next(&iterator, kb, vb, 0);
            size_t i = (pkey - (uint8_t*)m->pk) / kb;
            _map_undup(m, i, kb, vb);
        }
    }
    m->n = 0;
    const size_t capacity = m->a > 0 ? m->a : c;
    memset(m->bm, 0, ((capacity + 63) / 64) * sizeof(m->bm[0]));
}

void _map_free(void* mv, size_t c, size_t kb, size_t vb) {
    _map_clear(mv, c, kb, vb);
    map_t* m = mv;
    if (c == 1 && m->a != 0) { _map_set_pointers(m, 0, 0, 0, 0); m->a = 0; }
}

static inline size_t _map_hash(uint64_t key, size_t c) {
    key ^= key >> 33;
    key *= 0XFF51AFD7ED558CCDuLL;
    key ^= key >> 33;
    key *= 0XC4CEB9FE1A85EC53uLL;
    key ^= key >> 33;
    return (size_t)(key % c);
}

#define map_hash(m, k, c) ((m)->hash ? (m)->hash(k, c) : _map_hash(k, c))

size_t _map_str_hash(uint64_t key, size_t c) {
    const char* s =(const char*)(uintptr_t)key;
    uint64_t h = 0xcbf29ce484222325uLL; // FNV-1a 64-bit offset basis
    if (s) { // map_str(const char*, const char*) allow null keys and values
        while (*s) {
            h ^= (uint8_t)(*s++);
            h *= 0x100000001b3uLL; // FNV-1a 64-bit prime
        }
    }
    return (size_t)(h % c);
}

int _map_str_cmp(uint64_t k0, uint64_t k1) {
    return k0 == k1 ? 0 :
        strcmp((const char*)(uintptr_t)k0, (const char*)(uintptr_t)k1);
}

#define _map_bm_incl(bm, i) do { bm[i / 64] |=  (1uLL << (i % 64)); } while (0)
#define _map_bm_excl(bm, i) do { bm[i / 64] &= ~(1uLL << (i % 64)); } while (0)
#define _map_bm_is_empty(bm, i) ((bm[(i) / 64] & (1uLL << ((i) % 64))) == 0)

#define _map_is_empty(m, i) _map_bm_is_empty((m)->bm, i)

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

#define _map_set_entry(k, v, i, pkey, pval, kb, vb) do { \
    _map_set_at(k, i, pkey, kb);                         \
    _map_set_at(v, i, pval, vb);                         \
} while (0)

#define _map_move_entry(dk, dv, i, sk, sv, j, kb, vb) do { \
    _map_move(dk, i, sk, j, kb);                           \
    _map_move(dv, i, sv, j, vb);                           \
} while (0)

const void* _map_get(const void* mv, const size_t c,
                     const size_t kb, const size_t vb, const void* pkey) {
    const map_t* m = mv;
    const uint8_t* k = (const uint8_t*)m->pk;
    const uint8_t* v = (const uint8_t*)m->pv;
    const uint64_t key = _map_key(pkey, kb);
    const size_t h = map_hash(m, key, c);
    size_t i = h; // start
    while (!_map_is_empty(m, i)) {
        const uint64_t ki = _map_key_at(k, kb, i);
        if (m->cmp ? m->cmp(ki, key) == 0 : ki == key) {
            return v + i * vb;
        } else {
            i = (i + 1) % c;
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

static bool _map_grow(map_t* m, const size_t kb, const size_t vb) {
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
            while (!_map_bm_is_empty(bm, h)) {
                h = (h + 1) % a;  // new kv map cannot be full
            }
            _map_move_entry(pk, pv, h, k, v, i, kb, vb);
            _map_bm_incl(bm, h);
            _map_link(&head, pn, h);
            node = node->next;
        } while (node != m->head);
        m->head = head;
        _map_set_pointers(m, pk, pv, bm, pn);
        m->a = a;
        return true;
    }
}

bool _map_put(void* mv, const size_t capacity, const size_t kb, const size_t vb,
              const void* pkey, const void* pval) {
    map_t* m = mv;
    size_t c = capacity;
    if (m->a != 0) {
        const size_t c34 = c * 3 / 4;
        if (m->n >= c34) {
            if (!_map_grow(m, kb, vb)) { return false; } // fatal already called
            c = m->a;
        }
    }
    uint64_t key;
    void* key_dup = 0;
    if (m->tag & map_keydup) {
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
    if (m->tag & map_valdup) {
        if (*(void**)pval) {
            val_dup = strdup(*(const char**)pval);
            if (!val_dup) { free(key_dup); _map_fatal_return_zero(_map_oom); }
        }
        pval = &val_dup;
    }
    uint8_t* k = (uint8_t*)m->pk;
    uint8_t* v = (uint8_t*)m->pv;
    const size_t h = map_hash(m, key, c);
    size_t i = h;
    while (!_map_is_empty(m, i)) {
        const uint64_t ki = _map_key_at(k, kb, i);
        if (m->cmp ? m->cmp(ki, key) == 0 : ki == key) {
            _map_undup_key(m, i, kb);
            _map_set_entry(k, v, i, pkey, pval, kb, vb);
            // m->mc is not incremented because key set is not changed
            return true;
        } else {
            i = (i + 1) % c;
            if (i == h) { _map_fatal_return_zero("map is full\n"); }
        }
    }
    _map_set_entry(k, v, i, pkey, pval, kb, vb);
    _map_link(&m->head, m->pn, i);
    _map_bm_incl(m->bm, i);
    m->n++;
    m->mc++;
    return true;
}

bool _map_delete(void* mv, const size_t c, size_t kb, size_t vb,
                 const void* pkey) {
    map_t* m = mv;
    uint8_t* k = (uint8_t*)m->pk;
    uint8_t* v = (uint8_t*)m->pv;
    const uint64_t key = _map_key(pkey, kb);
    size_t h = m->hash ? m->hash(key, c) : _map_hash(key, c);
    bool found = false;
    size_t i = h; // start
    while (!found && !_map_is_empty(m, i)) {
        const uint64_t ki = _map_key_at(k, kb, i);
        found = m->cmp ? m->cmp(ki, key) == 0 : ki == key;
        if (!found) {
            i = (i + 1) % c;
            if (i == h) { break; }
        }
    }
    if (found) {
//      m->bm[i / 64] &= ~(1uLL << (i % 64));
        _map_bm_excl(m->bm, i);
        _map_undup(m, i, kb, vb);
        _map_unlink(&m->head, m->pn, i);
        size_t x = i;
        for (;;) {
            x = (x + 1) % c;
            if (_map_is_empty(m, x)) { break; }
            assert(x != i); // because empty slot exists
            const uint64_t kx = _map_key_at(k, kb, x);
            h = m->hash ? m->hash(kx, c) : _map_hash(kx, c);
            const bool can_move = i <= x ? x < h || h <= i :
                                           x < h && h <= i;
            if (can_move) {
                _map_move_entry(k, v, i, k, v, x, kb, vb);
                _map_bm_incl(m->bm, i);
                _map_bm_excl(m->bm, x);
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

static void _map_print(void* mv, size_t c, size_t kb, size_t vb) {
    map_t* m = mv;
    if (m->head) {
        printf("head: %zd capacity: %zd entries: %zd\n",
               m->head - m->pn, c, m->n);
    } else {
        printf("head: null capacity: zd entries: %zd\n", c, m->n);
    }
    for (size_t i = 0; i < c; i++) {
        if (!_map_is_empty(m, i)) {
            const uint64_t key = _map_key_at(m->pk, kb, i);
            const size_t prev = m->pn[i].prev - m->pn;
            const size_t next = m->pn[i].next - m->pn;
            printf("[%3zd] k=%016llX .prev=%3zd .next=%3zd ", i, key, prev, next);
            for (size_t k = 0; k < vb; k++) { printf("%02X", m->pv[i * vb + k]); }
            const uint64_t h = _map_hash(key, c);
            printf(" hash=%lld\n", h);
        }
    }
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif // map_implementation

