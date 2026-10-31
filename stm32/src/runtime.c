// What the Arm toolchain leaves to the platform, for rccore on a single-threaded Cortex-M33.
#include <stdint.h>
#include <string.h>

#include "stm32u5xx.h"

// ── 64-bit atomics ──
// The M33 has no 64-bit exclusive load/store, so GCC calls these. With interrupts off, a
// plain access is atomic: nothing else runs.
static inline uint32_t lockIrq(void) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}
static inline void unlockIrq(uint32_t primask) { __set_PRIMASK(primask); }

uint64_t __atomic_load_8(const volatile void* p, int order) {
    (void) order;
    uint32_t k = lockIrq();
    uint64_t v = *(const volatile uint64_t*) p;
    unlockIrq(k);
    return v;
}
void __atomic_store_8(volatile void* p, uint64_t v, int order) {
    (void) order;
    uint32_t k = lockIrq();
    *(volatile uint64_t*) p = v;
    unlockIrq(k);
}
uint64_t __atomic_fetch_add_8(volatile void* p, uint64_t v, int order) {
    (void) order;
    uint32_t k = lockIrq();
    uint64_t old = *(volatile uint64_t*) p;
    *(volatile uint64_t*) p = old + v;
    unlockIrq(k);
    return old;
}
uint64_t __atomic_exchange_8(volatile void* p, uint64_t v, int order) {
    (void) order;
    uint32_t k = lockIrq();
    uint64_t old = *(volatile uint64_t*) p;
    *(volatile uint64_t*) p = v;
    unlockIrq(k);
    return old;
}
_Bool __atomic_compare_exchange_8(volatile void* p, void* expected, uint64_t desired, _Bool weak,
                                  int success, int failure) {
    (void) weak; (void) success; (void) failure;
    uint32_t k = lockIrq();
    uint64_t cur = *(volatile uint64_t*) p;
    _Bool ok = cur == *(uint64_t*) expected;
    if (ok) *(volatile uint64_t*) p = desired;
    else *(uint64_t*) expected = cur;
    unlockIrq(k);
    return ok;
}

// ── Thread-local storage ──
// ARM's TLS layout (variant 1): the thread pointer points at an 8-byte control block, and
// the variables follow it: .tdata's image, then .tbss zeroed. The linker script keeps the
// template in flash (__tdata_*, __tbss_*); the one thread's copy is built here.
extern const uint8_t __tdata_start[], __tdata_end[], __tbss_start[], __tbss_end[];
#define TLS_MAX 512
static uint8_t sTls[8 + TLS_MAX] __attribute__((aligned(8)));

// Built before any constructor runs (priority 101 is the first a program may use).
__attribute__((constructor(101))) static void tlsInit(void) {
    size_t data = (size_t) (__tdata_end - __tdata_start), bss = (size_t) (__tbss_end - __tbss_start);
    if (data + bss > TLS_MAX) __BKPT(0);   // grow TLS_MAX
    memcpy(sTls + 8, __tdata_start, data);
    memset(sTls + 8 + data, 0, bss);
}

// The ABI lets __aeabi_read_tp change r0 and nothing else (GCC keeps values in r1-r3 across
// the call), so it can't be C.
__attribute__((naked)) void* __aeabi_read_tp(void) {
    __asm volatile("ldr r0, =sTls\n"
                   "bx lr\n");
}
