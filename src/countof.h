// SPDX-License-Identifier: 0BSD
//
// C2y countof(). GCC >= 15 has the _Countof keyword in every C language
// mode and ships the macro spelling in <stdcountof.h>; everything else --
// older compilers, C++, clangd's host clang -- gets the classic expansion.

#ifndef P96CTS_COUNTOF_H
#define P96CTS_COUNTOF_H

#if defined __has_include && !defined __cplusplus
#if __has_include(<stdcountof.h>)
#include <stdcountof.h>
#endif
#endif

#ifndef countof
#define countof(a) (sizeof (a) / sizeof (a)[0])
#endif

#endif
