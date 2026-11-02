/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AB32_NIMBLE_NPL_OS_H
#define AB32_NIMBLE_NPL_OS_H
#include <rtthread.h>
#include <rthw.h>
#define BLE_NPL_OS_ALIGNMENT 4
#define BLE_NPL_TIME_FOREVER UINT32_MAX
typedef uint32_t ble_npl_time_t;
typedef int32_t ble_npl_stime_t;
struct ble_npl_eventq;
struct ble_npl_event {
    rt_list_t link;
    struct ble_npl_eventq *owner;
    ble_npl_event_fn *fn;
    void *arg;
};
struct ble_npl_eventq { rt_list_t events; struct rt_semaphore wake; };
struct ble_npl_mutex { struct rt_mutex lock; };
struct ble_npl_sem { struct rt_semaphore sem; };
struct ble_npl_callout {
    struct rt_timer timer;
    struct ble_npl_eventq *evq;
    struct ble_npl_event ev;
    ble_npl_time_t expiry;
};
#endif
