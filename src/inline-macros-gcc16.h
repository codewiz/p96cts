// SPDX-License-Identifier: 0BSD
//
// Temporary workaround for the NDK inline macros losing an argument on gcc 13
// and later. Delete this once toolchains ship the regenerated inline headers.
//
// LP7NR and friends pass each argument in a local register variable bound to a
// hard register, and take the type from the fd, so a const-qualified parameter
// produces:
//
//     register const PLANEPTR _n2 __asm("a0") = _v2;
//
// The GCC manual says not to do that: with const, the compiler may substitute
// the variable with its initializer in the asm, and the operand can then end up
// in a different register. gcc 6.5 never did; 13.4 and later do, at -O1 and
// above, whenever the argument is a compile-time constant. So
// BltPattern(rp, NULL, ...) passes whatever a0 happened to hold, silently.
//
//     https://gcc.gnu.org/onlinedocs/gcc/Local-Register-Variables.html
//     https://gcc.gnu.org/bugzilla/show_bug.cgi?id=126552
//
// The real fix is sfdc's m68k-gcc-amigaos target (in bebbo's amiga-gcc), whose
// macros evaluate every argument into a plain temporary before the register
// variables, keep them unqualified, and name the base as an input of the jsr
// asm itself. This header is that generator's output for BltPattern, verbatim,
// so it works on any gcc from 13 up, in C and C++, with no special flags. On a
// toolchain whose headers already are the new scheme the redefinition is
// identical text and changes nothing.

#ifndef P96CTS_INLINE_MACROS_GCC16_H
#define P96CTS_INLINE_MACROS_GCC16_H

#include <proto/graphics.h>

#if defined(__GNUC__) && __GNUC__ >= 13

#ifndef GRAPHICS_BASE_NAME
#define GRAPHICS_BASE_NAME GfxBase
#endif

#undef BltPattern
#undef __BltPattern_base

#define __BltPattern_base(__in_base, ___rp, ___mask, ___xMin, ___yMin, ___xMax, ___yMax, ___maskBPR) ({\
  struct RastPort * __p____rp = (struct RastPort *)(___rp);\
  PLANEPTR __p____mask = (PLANEPTR)(___mask);\
  WORD __p____xMin = (WORD)(___xMin);\
  WORD __p____yMin = (WORD)(___yMin);\
  WORD __p____xMax = (WORD)(___xMax);\
  WORD __p____yMax = (WORD)(___yMax);\
  UWORD __p____maskBPR = (UWORD)(___maskBPR);\
  register struct RastPort * __v0 __asm("a1") = __p____rp;\
  register PLANEPTR __v1 __asm("a0") = __p____mask;\
  register WORD __v2 __asm("d0") = __p____xMin;\
  register WORD __v3 __asm("d1") = __p____yMin;\
  register WORD __v4 __asm("d2") = __p____xMax;\
  register WORD __v5 __asm("d3") = __p____yMax;\
  register UWORD __v6 __asm("d4") = __p____maskBPR;\
  __asm volatile (\
                   "jsr %%a6@(-312:W)\n"\
                   : "+a"(__v0), "+a"(__v1), "+d"(__v2), "+d"(__v3)\
                   : "a"(__in_base), "d"(__v4), "d"(__v5), "d"(__v6)\
                   : "fp0", "fp1", "cc", "memory" );\
})

#define BltPattern(___rp, ___mask, ___xMin, ___yMin, ___xMax, ___yMax, ___maskBPR) ({\
  register void *const __v_base __asm("a6") = GRAPHICS_BASE_NAME;\
  __BltPattern_base(__v_base, ___rp, ___mask, ___xMin, ___yMin, ___xMax, ___yMax, ___maskBPR);\
})

#endif /* __GNUC__ >= 13 */

#endif /* P96CTS_INLINE_MACROS_GCC16_H */
