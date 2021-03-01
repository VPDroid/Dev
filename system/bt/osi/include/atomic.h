/******************************************************************************
 *
 *  Copyright (C) 2014 Google, Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Set of atomic operations to work on data structures.
// The atomic type data should only be manipulated by these functions.
// This uses the gcc built in functions.
//  gcc doc section 6.50 "Built-in functions for memory model aware atomic operations"
//
// Data types:
// atomic_<name>_t
//
// e.g.
//  atomic_s32_t for a signed 32 bit integer.
//  atomic_u32_t for an unsigned 32 bit integer.
//
// Functions:
// atomic_<operation>_<name>(atomic_t *, ...)
//
// e.g.
//  uint32_t atomic_dec_prefix_u32(volatile atomic_u32_t *atomic)
//
// The prefix/postfix classification corresponds which value is returned
// from the function call, the old or the new one.

#if defined(GCC_VERSION) && GCC_VERSION < 40700
#warning "Atomics not supported"
#endif

#define ATOMIC_TYPE(name, type) \
  struct atomic_##name { \
    type _val; \
}; \
typedef struct atomic_##name atomic_##name##_t;

#define ATOMIC_STORE(name, type, sz) \
static inline void atomic_store_##name(volatile atomic_##name##_t *atomic, type val) { \
  __atomic_store_##sz(&atomic->_val, val, __ATOMIC_SEQ_CST); \
}

#define ATOMIC_LOAD(name, type, sz) \
static inline type atomic_load_##name(volatile atomic_##name##_t *atomic) { \
  return __atomic_load_##sz(&atomic->_val, __ATOMIC_SEQ_CST); \
}

// Returns value after operation, e.g. new value
#define ATOMIC_INC_PREFIX(name, type, sz) \
static inline type atomic_inc_prefix_##name(volatile atomic_##name##_t *atomic) { \
  return __atomic_add_fetch_##sz(atomic, 1, __ATOMIC_SEQ_CST); \
}

// Returns value after operation, e.g. new value
#define ATOMIC_DEC_PREFIX(name, type, sz) \
static inline type atomic_dec_prefix_##name(volatile atomic_##name##_t *atomic) { \
  return __atomic_sub_fetch_##sz(atomic, 1, __ATOMIC_SEQ_CST); \
}

// Returns value before operation, e.g. old value
#define ATOMIC_INC_POSTFIX(name, type, sz) \
static inline type atomic_inc_postfix_##name(volatile atomic_##name##_t *atomic) { \
  return __atomic_fetch_add_##sz(atomic, 1, __ATOMIC_SEQ_CST); \
}

// Returns value before operation, e.g. old value
#define ATOMIC_DEC_POSTFIX(name, type, sz) \
static inline type atomic_dec_postfix_##name(volatile atomic_##name##_t *atomic) { \
  return __atomic_fetch_sub_##sz(atomic, 1, __ATOMIC_SEQ_CST); \
}

// Returns value after operation, e.g. new value
#define ATOMIC_ADD(name, type, sz) \
static inline type atomic_add_##name(volatile atomic_##name##_t *atomic, type val) { \
  return __atomic_add_fetch_##sz(atomic, val, __ATOMIC_SEQ_CST); \
}

// Returns value after operation, e.g. new value
#define ATOMIC_SUB(name, type, sz) \
static inline type atomic_sub_##name(volatile atomic_##name##_t *atomic, type val) { \
  return __atomic_sub_fetch_##sz(atomic, val, __ATOMIC_SEQ_CST); \
}

#define ATOMIC_MAKE(name, type, sz) \
  ATOMIC_TYPE(name, type) \
  ATOMIC_STORE(name, type, sz) \
  ATOMIC_LOAD(name, type, sz) \
  ATOMIC_INC_PREFIX(name, type, sz) \
  ATOMIC_DEC_PREFIX(name, type, sz) \
  ATOMIC_INC_POSTFIX(name, type, sz) \
  ATOMIC_DEC_POSTFIX(name, type, sz) \
  ATOMIC_ADD(name, type, sz) \
  ATOMIC_SUB(name, type, sz)

ATOMIC_MAKE(s32, int32_t, 4)
ATOMIC_MAKE(u32, uint32_t, 4)
ATOMIC_MAKE(s64, int64_t, 8)
ATOMIC_MAKE(u64, uint64_t, 8)
