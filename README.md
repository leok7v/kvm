# KVM: Key-Value Map in C

The `kvm` single file header library provides simple, lightweight 
implementation of a key-value map (dictionary) for as C23 code, 
with support for dynamic heap memory allocation and custom 
key/value types.

Key type values should be equal comparable by ```memcmp()```.

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
    kvm_clar(&map); // no necessary since nothing is allocated
    return 0;    
}
```

To create a heap allocated key-value map with initial capacity 16, 
use the `kvm_heap` macro with the desired  types for the keys and values. 
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

