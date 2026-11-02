/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <rtthread.h>
#include "drv_bluetooth.h"
/* Host and diagnostic modes are mutually exclusive until reboot. */
rt_bool_t ab32_btctrl_available(void);
rt_err_t ab32_btctrl_start_host(drv_bt_hci_rx_cb_t receive);
/* Success transfers arg to run() in the controller worker; failure retains it. */
rt_err_t ab32_btctrl_submit(void (*run)(void *), void *arg);
