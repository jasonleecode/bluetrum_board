/* SPDX-License-Identifier: Apache-2.0
 * NimBLE-local libc adapter. Keep assertions without newlib stdio/exit/signal.
 */
#include <rtthread.h>
#undef assert
#define assert(expression) RT_ASSERT(expression)
