# KVM: Key-Value Map in C

The `kvm` single file header library provides simple, lightweight 
implementation of a key-value map (dictionary) for as C23 code, 
with support for dynamic heap memory allocation and custom 
key/value types.

Keys will be compared by ```memcmp()```.

## Features

- Fixed size or dynamically allocated and automatically growing maps
- Supports variety of fixed key and value types
- Configurable error handling

## Usage

### Creating a KVM Map

To create a fixed size key-value map, use the `kvm` macro with the desired 
types for the keys and values and capacity maximum number of entries. 

```c
#include "kvm.h"

int main(void) {
    kvm_fatalist = true;
    kvm(int, double, 16) map;
    kvm_init(&map); // may return false if initialization fails
    kvm_put(&map, 42, 3.1415); // return false if put fails
    const double* e = kvm_get(&map, 42);
    printf("map[42]: %f\n", *e);
    kvm_delete(&map, 42); // returns true if item existed and has been deleted
    assert(!kvm_get(&map, 42));
    kvm_clear(&map); // no necessary since nothing is allocated
    return 0;    
}
```

To create a heap allocated key-value map use ```kvm(int, double)``` 
and ```kvm_alloc(&map, 16);``` for initial capacity of 16 entries. 
Map will be resizable using malloc()/free():

```c
#include "kvm.h"

int main(void) {
    kvm_fatalist = true;
    kvm(int, double) map; // dynamic map
    kvm_alloc(&map, 16);  // allocated 16 inital entries on the heap
    kvm_put(&map, 42, 3.1415);
    const double* e = kvm_get(&map, 42);
    printf("map[42]: %f\n", *e);
    kvm_delete(&map, 42);
    assert(!kvm_get(&map, 42));
    kvm_free(&map);
    return 0;    
}
```

## map: Key-Value map supporting strcmp() and strdup():

```c
#include "map.h"

int main(void) {
    map_fatalist = true;
    map(const char*, const char*, map_heap, map_strdup) m;
    map_alloc(&m, 4);
    kvm_put(&map, "Hello", "World");
    kvm_put(&map, "Goodbye", "Universe");
    const char* *e = kvm_get(&map, "Hello");
    printf("map[\"Hello\"]: \"%s\"\n", *e);
    const char* *e = kvm_get(&map, "Goodbye");
    printf("map[\"Goodbye\"]: \"%s\"\n", *e);
    kvm_delete(&map, "Hello");
    assert(!kvm_get(&map, "Hello"));
    assert(map.n == 1);
    map_clear(&map);
    assert(map.n == 0);
    kvm_free(&map);
    return 0;    
}
```

## Performance measurements:

```c
    enum { n = 2 * 1024 * 1024 };
    static size_t index[n]; // randomly permutated
    static uint64_t k[n]; // random initialized
    static uint64_t v[n]; // random initialized
    static kvm(uint64_t, uint64_t, n + n / 4) m;
    kvm_init(&m);
    kvm_put(&m, k[index[i]], v[index[i]]);
    kvm_get(&m, k[index[i]]);
    kvm_delete(&m, k[index[i]]);
```

```c
    enum { n = 2 * 1024 * 1024 };
    static size_t index[n]; // randomly permutated
    static uint64_t k[n]; // random initialized
    static uint64_t v[n]; // random initialized
    static kvm(uint64_t, uint64_t) m;
    kvm_alloc(&m, 16); // allocated and growing on the heap
    kvm_put(&m, k[index[i]], v[index[i]]);
    kvm_get(&m, k[index[i]]);
    kvm_delete(&m, k[index[i]]);
    kvm_free(&m);
```

## MacBook Air M3 2024 ARM64 Release build results (μs is microseconds):

```
static kvm(uint64_t, uint64_t, 20971520) m;
kvm_put   : 0.061μs
kvm_get   : 0.050μs
kvm_delete: 0.096μs
```

```
static kvm(uint64_t, uint64_t) m; // heap
kvm_put   : 0.141μs
kvm_get   : 0.039μs
kvm_delete: 0.076μs
```


run to run performance fluctuations are due to 
L1/L2/L3 caches hit/miss variations.

## C++ unordered_map performance comparison (heap strdup):

```
std::unordered_map<uint64_t, uint64_t> reserved(2621440)
unordered_map::put   : 0.191μs
unordered_map::get   : 0.082μs
unordered_map::delete: 0.266μs
```

```
map(uint64_t, uint64_t, 2621440)
map_put   : 0.098μs
map_get   : 0.060μs
map_delete: 0.127μs
```

```
unordered_map<std::string, std::string>
unordered_map::put   : 0.499μs
unordered_map::get   : 0.774μs
unordered_map::delete: 0.658μs
```

```
map(const char*, const char*, map_heap, map_strdup)
map_put   : 0.652μs
map_get   : 0.386μs
map_delete: 0.532μs
```
