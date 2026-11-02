/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AB32_NIMBLE_NPL_LOG_H
#define AB32_NIMBLE_NPL_LOG_H
#define BLE_NPL_LOG_IMPL(lvl) \
    static inline void _BLE_NPL_LOG_CAT(BLE_NPL_LOG_MODULE, \
        _BLE_NPL_LOG_CAT(_, lvl))(const char *fmt, ...) { (void)fmt; }
#endif
