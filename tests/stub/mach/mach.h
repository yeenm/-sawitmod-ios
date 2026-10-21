#pragma once
// 语法自检用的 mach/mach.h（Windows 没有）。
#include <cstdint>

typedef int kern_return_t;
typedef int vm_prot_t;
typedef uintptr_t vm_address_t;
typedef uintptr_t vm_size_t;
typedef unsigned int mach_port_t;
typedef int boolean_t;

#define KERN_SUCCESS 0
#define VM_PROT_NONE 0
#define VM_PROT_READ 1
#define VM_PROT_WRITE 2
#define VM_PROT_EXECUTE 4
#define VM_PROT_COPY 16
#define MACH_PORT_NULL 0

inline mach_port_t mach_task_self() { return 0; }
inline kern_return_t mach_port_deallocate(mach_port_t, mach_port_t) { return 0; }
inline kern_return_t vm_protect(mach_port_t, vm_address_t, vm_size_t, boolean_t, vm_prot_t) {
    return 0;
}
inline kern_return_t vm_region_64(mach_port_t, vm_address_t *, vm_size_t *, int, int *,
                                  unsigned int *, mach_port_t *) {
    return 1;
}

#define VM_REGION_BASIC_INFO_64 9
#define VM_REGION_BASIC_INFO_COUNT_64 10

struct vm_region_basic_info_data_64_t {
    vm_prot_t protection;
};
typedef int *vm_region_info_t;
