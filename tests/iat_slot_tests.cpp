// SPDX-License-Identifier: GPL-3.0-only
#include "iat_slot.h"
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
namespace {
struct Page {
    std::byte *data;
    size_t size;
    Page() {
#ifdef _WIN32
        SYSTEM_INFO info{}; GetSystemInfo(&info); size=info.dwPageSize;
        data=static_cast<std::byte *>(VirtualAlloc(nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        assert(data);
#else
        const long result=sysconf(_SC_PAGESIZE); assert(result>0); size=static_cast<size_t>(result);
        void *mapping=mmap(nullptr,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        assert(mapping!=MAP_FAILED); data=static_cast<std::byte *>(mapping);
#endif
    }
    void protect(bool writable) {
#ifdef _WIN32
        DWORD previous=0;
        assert(VirtualProtect(data,size,writable?PAGE_READWRITE:PAGE_READONLY,&previous));
#else
        assert(mprotect(data,size,PROT_READ|(writable?PROT_WRITE:0))==0);
#endif
    }
    ~Page() {
#ifdef _WIN32
        VirtualFree(data,0,MEM_RELEASE);
#else
        munmap(data,size);
#endif
    }
};
}
int main() {
    Page page;
    int predecessor=1,wrapper=2,replacement=3;
    auto **first=reinterpret_cast<void **>(page.data);
    auto **last=reinterpret_cast<void **>(page.data+page.size-sizeof(void *));
    std::construct_at(first,static_cast<void *>(&predecessor));
    std::construct_at(last,static_cast<void *>(nullptr));
    page.protect(false);
    // These checks must not issue a write, including when the slot is null.
    assert(cabinet::load_iat_slot(first)==&predecessor);
    assert(cabinet::load_iat_slot(last)==nullptr);
    page.protect(true);
    __atomic_store_n(first,static_cast<void *>(&wrapper),__ATOMIC_RELEASE);
    page.protect(false);
    assert(cabinet::load_iat_slot(first)==&wrapper);
    // Monitoring must see a later owner after its writer restores read-only protection.
    page.protect(true);
    __atomic_store_n(first,static_cast<void *>(&replacement),__ATOMIC_RELEASE);
    page.protect(false);
    assert(cabinet::load_iat_slot(first)==&replacement);
    assert(cabinet::load_iat_slot(first)!=&wrapper);
    std::puts("read-only IAT slots: null, predecessor, wrapper and replacement loads passed");
}
