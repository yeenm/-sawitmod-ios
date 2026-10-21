#pragma once

// Target functions remain hidden from the ELF dynamic export table.  The
// special Virbox input build keeps the regular symbol table so Protector can
// list these stable names, while normal release builds still strip everything.
// =========================================================
//  OLLVM TARGETED FUNCTION OBFUSCATION
//  - fla: Control Flow Flattening (State-machine loop)
//  - sub: Instruction Substitution (Bitwise expansion)
//  - bcf: Bogus Control Flow (Dead branches / opaque predicates)
// =========================================================
#if !defined(__clang__) && !defined(__GNUC__)
// 非 Clang/GCC（例如本地 MSVC 语法自检）不认识 __attribute__，降级为空。
#  define OLLVM_SEC_CRITICAL
#  define OLLVM_SEC_FLATTEN
#  define VIRBOX_TARGET_ATTR
#else
#define OLLVM_SEC_CRITICAL __attribute__((annotate("sub"), annotate("fla"), annotate("bcf"), noinline, visibility("hidden")))
#define OLLVM_SEC_FLATTEN  __attribute__((annotate("fla"), annotate("bcf"), noinline, visibility("hidden")))

#if defined(SAWIT_VIRBOX_BUILD) && SAWIT_VIRBOX_BUILD
#  define VIRBOX_TARGET_ATTR \
    __attribute__((noinline, used, retain, visibility("hidden"), annotate("sub"), annotate("fla"), annotate("bcf")))
#else
#  define VIRBOX_TARGET_ATTR \
    __attribute__((noinline, visibility("hidden"), annotate("sub"), annotate("fla"), annotate("bcf")))
#endif




#endif  // __clang__/__GNUC__
