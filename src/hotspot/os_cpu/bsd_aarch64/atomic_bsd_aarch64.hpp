/*
 * Copyright (c) 1999, 2019, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014, 2021, Red Hat Inc. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef OS_CPU_BSD_AARCH64_VM_ATOMIC_BSD_AARCH64_HPP
#define OS_CPU_BSD_AARCH64_VM_ATOMIC_BSD_AARCH64_HPP

// Implementation of class atomic

// Note that memory_order_conservative requires a full barrier after atomic stores.
// See https://patchwork.kernel.org/patch/3575821/

#define FULL_MEM_BARRIER  __sync_synchronize()
#define READ_MEM_BARRIER  __atomic_thread_fence(__ATOMIC_ACQUIRE);
#define WRITE_MEM_BARRIER __atomic_thread_fence(__ATOMIC_RELEASE);

#ifdef __APPLE__

template<size_t byte_size>
struct Atomic::PlatformAdd
  : Atomic::AddAndFetch<Atomic::PlatformAdd<byte_size> >
{
  template<typename I, typename D>
  D add_and_fetch(I add_value, D volatile* dest, atomic_memory_order order) const {
    D res = __atomic_add_fetch(dest, add_value, __ATOMIC_RELEASE);
    FULL_MEM_BARRIER;
    return res;
  }
};

template<size_t byte_size>
template<typename T>
inline T Atomic::PlatformXchg<byte_size>::operator()(T exchange_value,
                                                     T volatile* dest,
                                                     atomic_memory_order order) const {
  STATIC_ASSERT(byte_size == sizeof(T));
  T res = __atomic_exchange_n(dest, exchange_value, __ATOMIC_RELEASE);
  FULL_MEM_BARRIER;
  return res;
}

template<size_t byte_size>
template<typename T>
inline T Atomic::PlatformCmpxchg<byte_size>::operator()(T exchange_value,
                                                        T volatile* dest,
                                                        T compare_value,
                                                        atomic_memory_order order) const {
  STATIC_ASSERT(byte_size == sizeof(T));
  if (order == memory_order_relaxed) {
    T value = compare_value;
    __atomic_compare_exchange(dest, &value, &exchange_value, /*weak*/false,
                              __ATOMIC_RELAXED, __ATOMIC_RELAXED);
    return value;
  } else {
    T value = compare_value;
    FULL_MEM_BARRIER;
    __atomic_compare_exchange(dest, &value, &exchange_value, /*weak*/false,
                              __ATOMIC_RELAXED, __ATOMIC_RELAXED);
    FULL_MEM_BARRIER;
    return value;
  }
}

#else // __APPLE__

#include "atomic_aarch64.hpp"
#include "runtime/vm_version.hpp"

// Call one of the stubs from C++. This uses the C calling convention,
// but this asm definition is used in order only to clobber the
// registers we use. If we called the stubs via an ABI call we'd have
// to save X0 - X18 and most of the vectors.
//
// This really ought to be a template definition, but see GCC Bug
// 33661, template methods forget explicit local register asm
// vars. The problem is that register specifiers attached to local
// variables are ignored in any template function.
inline uint64_t bare_atomic_fastcall(address stub, volatile void *ptr, uint64_t arg1, uint64_t arg2 = 0) {
  register uint64_t reg0 __asm__("x0") = (uint64_t)ptr;
  register uint64_t reg1 __asm__("x1") = arg1;
  register uint64_t reg2 __asm__("x2") = arg2;
  register uint64_t reg3 __asm__("x3") = (uint64_t)stub;
  register uint64_t result __asm__("x0");
  asm volatile(// "stp x29, x30, [sp, #-16]!;"
               " blr %1;"
               // " ldp x29, x30, [sp], #16 // regs %0, %1, %2, %3, %4"
               : "=r"(result), "+r"(reg3), "+r"(reg2)
               : "r"(reg1), "0"(reg0) : "x8", "x9", "x30", "cc", "memory");
  return result;
}

template <typename F, typename D, typename T1>
inline D atomic_fastcall(F stub, volatile D *dest, T1 arg1) {
  return (D)bare_atomic_fastcall(CAST_FROM_FN_PTR(address, stub),
                                 dest, (uint64_t)arg1);
}

template <typename F, typename D, typename T1, typename T2>
inline D atomic_fastcall(F stub, volatile D *dest, T1 arg1, T2 arg2) {
  return (D)bare_atomic_fastcall(CAST_FROM_FN_PTR(address, stub),
                                 dest, (uint64_t)arg1, (uint64_t)arg2);
}

template<size_t byte_size>
struct Atomic::PlatformAdd
  : Atomic::FetchAndAdd<Atomic::PlatformAdd<byte_size> >
{
  template<typename I, typename D>
  D fetch_and_add(I add_value, D volatile* dest, atomic_memory_order order) const;
};

template<>
template<typename I, typename D>
inline D Atomic::PlatformAdd<4>::fetch_and_add(I add_value,
                                               D volatile* dest,
                                               atomic_memory_order order) const {
  STATIC_ASSERT(4 == sizeof(I));
  STATIC_ASSERT(4 == sizeof(D));
  D old_value
    = atomic_fastcall(aarch64_atomic_fetch_add_4_impl, dest, add_value);
  return old_value;
}

template<>
template<typename I, typename D>
inline D Atomic::PlatformAdd<8>::fetch_and_add(I add_value,
                                               D volatile* dest,
                                               atomic_memory_order order) const {
  STATIC_ASSERT(8 == sizeof(I));
  STATIC_ASSERT(8 == sizeof(D));
  D old_value
    = atomic_fastcall(aarch64_atomic_fetch_add_8_impl, dest, add_value);
  return old_value;
}

template<>
template<typename T>
inline T Atomic::PlatformXchg<4>::operator()(T exchange_value,
                                             T volatile* dest,
                                             atomic_memory_order order) const {
  STATIC_ASSERT(4 == sizeof(T));
  T old_value = atomic_fastcall(aarch64_atomic_xchg_4_impl, dest, exchange_value);
  return old_value;
}

template<>
template<typename T>
inline T Atomic::PlatformXchg<8>::operator()(T exchange_value,
                                             T volatile* dest,
                                             atomic_memory_order order) const {
  STATIC_ASSERT(8 == sizeof(T));
  T old_value = atomic_fastcall(aarch64_atomic_xchg_8_impl, dest, exchange_value);
  return old_value;
}

template<>
template<typename T>
inline T Atomic::PlatformCmpxchg<1>::operator()(T exchange_value,
                                                T volatile* dest,
                                                T compare_value,
                                                atomic_memory_order order) const {
  STATIC_ASSERT(1 == sizeof(T));
  aarch64_atomic_stub_t stub;
  switch (order) {
  case memory_order_relaxed:
    stub = aarch64_atomic_cmpxchg_1_relaxed_impl; break;
  default:
    stub = aarch64_atomic_cmpxchg_1_impl; break;
  }

  return atomic_fastcall(stub, dest, compare_value, exchange_value);
}

template<>
template<typename T>
inline T Atomic::PlatformCmpxchg<4>::operator()(T exchange_value,
                                                T volatile* dest,
                                                T compare_value,
                                                atomic_memory_order order) const {
  STATIC_ASSERT(4 == sizeof(T));
  aarch64_atomic_stub_t stub;
  switch (order) {
  case memory_order_relaxed:
    stub = aarch64_atomic_cmpxchg_4_relaxed_impl; break;
  default:
    stub = aarch64_atomic_cmpxchg_4_impl; break;
  }

  return atomic_fastcall(stub, dest, compare_value, exchange_value);
}

template<>
template<typename T>
inline T Atomic::PlatformCmpxchg<8>::operator()(T exchange_value,
                                                T volatile* dest,
                                                T compare_value,
                                                atomic_memory_order order) const {
  STATIC_ASSERT(8 == sizeof(T));
  aarch64_atomic_stub_t stub;
  switch (order) {
  case memory_order_relaxed:
    stub = aarch64_atomic_cmpxchg_8_relaxed_impl; break;
  default:
    stub = aarch64_atomic_cmpxchg_8_impl; break;
  }

  return atomic_fastcall(stub, dest, compare_value, exchange_value);
}

#endif // __APPLE__

#endif // OS_CPU_BSD_AARCH64_VM_ATOMIC_BSD_AARCH64_HPP
