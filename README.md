# KVM: Key-Value Map in C

The `kvm` library provides a lightweight implementation of a key-value map (dictionary) 
for C programs, with support for dynamic memory allocation and custom key/value types.

## Features

- Fixed size or dynamically allocated and automatically growing maps
- Supports custom key and value types
- Configurable error handling

## Usage

### Creating a KVM Map

To create a fixed size key-value map, use the `kvm` macro with the desired 
types for the keys and values and capacity maximum number of entries. 

```c
#include "kvm.h"

int main(void) {
    kvm_errors_are_fatal = true;
    kvm_fixed(int, double, 16) map;
    kvm_init(&map); // return false if initialization fails
    kvm_put(&map, 42, 3.1415); // return false if put fails
    const double* e = kvm_get(&map, 42);
    printf("map[42]: %f\n", *e);
    kvm_delete(&map, 42); // returns true if item existed and has been deleted
    assert(!kvm_get(&map, 42));
    return 0;    
}
```

To create a heap allocated key-value map with initial capacity 16, 
use the `kvm_heap` macro with the desired  types for the keys and values. 
Map will be resizable using malloc()/free():

```c
#include "kvm.h"

int main(void) {
    kvm_errors_are_fatal = true;
    kvm_heap(int, double) map;
    kvm_alloc(&map, 16);
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
    enum { n = 16 * 1024 * 1024 };
    static size_t index[n]; // randomly permutated
    static uint64_t k[n]; // random initialized
    static uint64_t v[n]; // random initialized
    // 75% occupancy:
    static kvm_fixed(uint64_t, uint64_t, n + n / 4) m;
    // or heap allocated:
//  static kvm_heap(uint64_t, uint64_t) m;

    kvm_init(&m);
//  kvm_alloc(&m);
    ...
    kvm_put(&m, k[index[i]], v[index[i]]);
    ...
    kvm_get(&m, k[index[i]]);
    ...
    kvm_delete(&m, k[index[i]]);
    ...
//  kvm_free(&m);
```

## MacBook Air M3 2024 ARM64 Release build results:

```
static kvm_fixed(uint64_t, uint64_t, 20971520) m;
kvm_put   : 0.108μs
kvm_get   : 0.065μs
kvm_delete: 0.134μs
```

```
static kvm_heap(uint64_t, uint64_t) m;
kvm_put   : 0.194μs
kvm_get   : 0.054μs
kvm_delete: 0.115μs
time in μs microseconds
```

run to run performance fluctuations are due to 
L1/L2/L3 caches hit/miss variations.

```
enum { n = 2 * 1024 * 1024 }; // slightly better performance

static kvm_fixed(uint64_t, uint64_t, 2621440) m;
kvm_put   : 0.080μs
kvm_get   : 0.050μs
kvm_delete: 0.098μs
static kvm_heap(uint64_t, uint64_t) m;
kvm_put   : 0.136μs
kvm_get   : 0.041μs
kvm_delete: 0.081μs
```