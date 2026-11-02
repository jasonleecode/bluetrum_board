#ifndef BT_RECOVERED_INTERNAL_H
#define BT_RECOVERED_INTERNAL_H
#include <stdint.h>
#include <stddef.h>
/* Raw offsets are the verified private controller ABI, not guessed structs. */
typedef uint16_t bt_u16_alias __attribute__((may_alias));
typedef uint32_t bt_u32_alias __attribute__((may_alias));
#define BT_B(p, n) (((uint8_t *)(uintptr_t)(p))[(n)])
#define BT_H(p, n) (*(bt_u16_alias *)((uint8_t *)(uintptr_t)(p) + (n)))
#define BT_W(p, n) (*(bt_u32_alias *)((uint8_t *)(uintptr_t)(p) + (n)))
#define BT_REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define BT_SEC(s) __attribute__((section(s), noinline, used))
#define BT_WEAK __attribute__((weak))
extern void *memcpy(void *, const void *, size_t);
extern void *memset(void *, int, size_t);
extern int memcmp(const void *, const void *, size_t);
extern void my_printf(const char *, ...);
#endif
