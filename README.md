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
    kvm(int, double, 16) map;
    kvm_init(&map); // return false if initialization fails
    kvm_put(&map, 42, 3.1415); // return false if put fails
    const double* e = kvm_get(&map, 42);
    printf("map[42]: %f\n", *e);
    kvm_delete(&map, 42); // returns true if item existed and has been deleted
    assert(!kvm_get(&map, 42));
    return 0;    
}
```

To create a fixed size key-value map, use the `kvm` macro with the desired 
types for the keys and values and zero as map fixed capacity. Map will become
resizable using malloc():
 

```c
#include "kvm.h"

int main(void) {
    kvm_errors_are_fatal = true;
    kvm(int, double, 0) map;
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

