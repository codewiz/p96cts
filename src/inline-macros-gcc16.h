// SPDX-License-Identifier: 0BSD
//
// Temporary workaround for the NDK inline macros losing an argument on gcc 13
// and later. Delete this once gcc or amiga-gcc carries the real fix.
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
// Hard register constraints are the documented replacement for register
// variables, so LP7NR_GCC16 below is LP7NR rewritten to use them. Nothing can
// be folded away, and the const stays on the argument type, so a caller passing
// a const variable still compiles: only the macro's internal copy is stripped,
// because a scratch register has to be a read-write operand and that needs a
// modifiable lvalue.
//
// Two requirements come with it:
//   - gcc 16 or later. gcc 15 has "{reg}" but no read-write form of it.
//   - -mlra, because m68k still defaults to the old reload pass and gcc rejects
//     hard register constraints with "hard register constraints are only
//     supported while using LRA".

#ifndef P96CTS_INLINE_MACROS_GCC16_H
#define P96CTS_INLINE_MACROS_GCC16_H

#include <proto/graphics.h>

// gcc 13 and 14 have neither hard register constraints nor -mlra, and gcc 15
// has no read-write form of them ("matching constraint not valid in output
// operand"), so there is nothing to substitute there. Refuse the build rather
// than emit the silently wrong call this header exists to prevent.
#if defined(__GNUC__) && __GNUC__ >= 13 && __GNUC__ < 16
#error "gcc 13 to 15 miscompile BltPattern(rp, NULL, ...): build with gcc 16+ or 6.5."
#endif

#if defined(__GNUC__) && __GNUC__ >= 16

// The m68k ABI lets a callee trash d0/d1/a0/a1 and preserve everything else, so
// those four are read-write operands and d2-d4 are plain inputs. Naming them as
// clobbers instead is not allowed: a register cannot be both an operand and a
// clobber.
#define LP7NR_GCC16(offs, name, t1, v1, r1, t2, v2, r2, t3, v3, r3, t4, v4, r4, \
                    t5, v5, r5, t6, v6, r6, t7, v7, r7, bn)                     \
    ({                                                                          \
        __typeof_unqual__(t1) _lp1 = (v1);                                      \
        __typeof_unqual__(t2) _lp2 = (v2);                                      \
        __typeof_unqual__(t3) _lp3 = (v3);                                      \
        __typeof_unqual__(t4) _lp4 = (v4);                                      \
        __typeof_unqual__(t5) _lp5 = (v5);                                      \
        __typeof_unqual__(t6) _lp6 = (v6);                                      \
        __typeof_unqual__(t7) _lp7 = (v7);                                      \
        void *_lpbase = (void *)(bn);                                           \
        __asm volatile("jsr a6@(-" #offs ":W)"                                  \
                       : "+{" #r1 "}"(_lp1), "+{" #r2 "}"(_lp2),                \
                         "+{" #r3 "}"(_lp3), "+{" #r4 "}"(_lp4)                 \
                       : "{" #r5 "}"(_lp5), "{" #r6 "}"(_lp6),                  \
                         "{" #r7 "}"(_lp7), "{a6}"(_lpbase)                     \
                       : "fp0", "fp1", "cc", "memory");                         \
    })

#undef BltPattern
#define BltPattern(___rp, ___mask, ___xMin, ___yMin, ___xMax, ___yMax, ___maskBPR) \
    LP7NR_GCC16(0x138, BltPattern, struct RastPort *, ___rp, a1,                   \
                CONST PLANEPTR, ___mask, a0, WORD, ___xMin, d0, WORD, ___yMin, d1, \
                WORD, ___xMax, d2, WORD, ___yMax, d3, UWORD, ___maskBPR, d4,       \
                GRAPHICS_BASE_NAME)

#endif // __GNUC__ >= 16

#endif // P96CTS_INLINE_MACROS_GCC16_H
