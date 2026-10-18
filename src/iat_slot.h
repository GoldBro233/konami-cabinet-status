// SPDX-License-Identifier: GPL-3.0-only
#pragma once
namespace cabinet {
// PE64 IAT entries are pointer-aligned. An ownership check must also work on
// PAGE_READONLY: compare-exchange is a write operation even when it changes nothing.
inline void *load_iat_slot(void *const volatile *slot) noexcept {
    static_assert(__atomic_always_lock_free(sizeof(void *),nullptr));
    return __atomic_load_n(slot,__ATOMIC_ACQUIRE);
}
}
