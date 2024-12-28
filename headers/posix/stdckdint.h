/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef __STDC_VERSION_STDCKDINT_H__
#define __STDC_VERSION_STDCKDINT_H__ 202311L


#define ckd_add(res, a, b) __builtin_add_overflow((a), (b), (r))
#define ckd_sub(res, a, b) __builtin_sub_overflow((a), (b), (r))
#define ckd_mul(res, a, b) __builtin_mul_overflow((a), (b), (r))


#endif	// __STDC_VERSION_STDCKDINT_H__
