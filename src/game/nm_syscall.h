/* NewMod, Arena.ai, 2026-11-21 (see CHANGES): native ET: Legacy VM call bridge.
 * Every argument is pointer-sized and the terminator prevents the engine
 * from reading arbitrary varargs past the end of a call on 64-bit hosts.
 * The original engine ignores the extra terminator.
 */
#ifndef NEWMOD_SYSCALL_H
#define NEWMOD_SYSCALL_H
#include <stdint.h>
#define NM_VM_END ((intptr_t)-1337)
#define NM_SYS_0(op) syscall((intptr_t)(op), NM_VM_END)
#define NM_SYS_1(op, a1) syscall((intptr_t)(op), (intptr_t)(a1), NM_VM_END)
#define NM_SYS_2(op, a1, a2) syscall((intptr_t)(op), (intptr_t)(a1), (intptr_t)(a2), NM_VM_END)
#define NM_SYS_3(op, a1, a2, a3) syscall((intptr_t)(op), (intptr_t)(a1), (intptr_t)(a2), (intptr_t)(a3), NM_VM_END)
#define NM_SYS_4(op, a1, a2, a3, a4) syscall((intptr_t)(op), (intptr_t)(a1), (intptr_t)(a2), (intptr_t)(a3), (intptr_t)(a4), NM_VM_END)
#define NM_SYS_5(op, a1, a2, a3, a4, a5) syscall((intptr_t)(op), (intptr_t)(a1), (intptr_t)(a2), (intptr_t)(a3), (intptr_t)(a4), (intptr_t)(a5), NM_VM_END)
#define NM_SYS_6(op, a1, a2, a3, a4, a5, a6) syscall((intptr_t)(op), (intptr_t)(a1), (intptr_t)(a2), (intptr_t)(a3), (intptr_t)(a4), (intptr_t)(a5), (intptr_t)(a6), NM_VM_END)
#define NM_SYS_7(op, a1, a2, a3, a4, a5, a6, a7) syscall((intptr_t)(op), (intptr_t)(a1), (intptr_t)(a2), (intptr_t)(a3), (intptr_t)(a4), (intptr_t)(a5), (intptr_t)(a6), (intptr_t)(a7), NM_VM_END)
#define NM_SYS_8(op, a1, a2, a3, a4, a5, a6, a7, a8) syscall((intptr_t)(op), (intptr_t)(a1), (intptr_t)(a2), (intptr_t)(a3), (intptr_t)(a4), (intptr_t)(a5), (intptr_t)(a6), (intptr_t)(a7), (intptr_t)(a8), NM_VM_END)
#define NM_SYS_9(op, a1, a2, a3, a4, a5, a6, a7, a8, a9) syscall((intptr_t)(op), (intptr_t)(a1), (intptr_t)(a2), (intptr_t)(a3), (intptr_t)(a4), (intptr_t)(a5), (intptr_t)(a6), (intptr_t)(a7), (intptr_t)(a8), (intptr_t)(a9), NM_VM_END)
#define NM_SYS_10(op, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) syscall((intptr_t)(op), (intptr_t)(a1), (intptr_t)(a2), (intptr_t)(a3), (intptr_t)(a4), (intptr_t)(a5), (intptr_t)(a6), (intptr_t)(a7), (intptr_t)(a8), (intptr_t)(a9), (intptr_t)(a10), NM_VM_END)
#define NM_SYS_11(op, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) syscall((intptr_t)(op), (intptr_t)(a1), (intptr_t)(a2), (intptr_t)(a3), (intptr_t)(a4), (intptr_t)(a5), (intptr_t)(a6), (intptr_t)(a7), (intptr_t)(a8), (intptr_t)(a9), (intptr_t)(a10), (intptr_t)(a11), NM_VM_END)
#define NM_SYS_12(op, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) syscall((intptr_t)(op), (intptr_t)(a1), (intptr_t)(a2), (intptr_t)(a3), (intptr_t)(a4), (intptr_t)(a5), (intptr_t)(a6), (intptr_t)(a7), (intptr_t)(a8), (intptr_t)(a9), (intptr_t)(a10), (intptr_t)(a11), (intptr_t)(a12), NM_VM_END)
#define NM_SYS_13(op, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) syscall((intptr_t)(op), (intptr_t)(a1), (intptr_t)(a2), (intptr_t)(a3), (intptr_t)(a4), (intptr_t)(a5), (intptr_t)(a6), (intptr_t)(a7), (intptr_t)(a8), (intptr_t)(a9), (intptr_t)(a10), (intptr_t)(a11), (intptr_t)(a12), (intptr_t)(a13), NM_VM_END)
#define NM_SYS_14(op, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) syscall((intptr_t)(op), (intptr_t)(a1), (intptr_t)(a2), (intptr_t)(a3), (intptr_t)(a4), (intptr_t)(a5), (intptr_t)(a6), (intptr_t)(a7), (intptr_t)(a8), (intptr_t)(a9), (intptr_t)(a10), (intptr_t)(a11), (intptr_t)(a12), (intptr_t)(a13), (intptr_t)(a14), NM_VM_END)
#define NM_SYS_15(op, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) syscall((intptr_t)(op), (intptr_t)(a1), (intptr_t)(a2), (intptr_t)(a3), (intptr_t)(a4), (intptr_t)(a5), (intptr_t)(a6), (intptr_t)(a7), (intptr_t)(a8), (intptr_t)(a9), (intptr_t)(a10), (intptr_t)(a11), (intptr_t)(a12), (intptr_t)(a13), (intptr_t)(a14), (intptr_t)(a15), NM_VM_END)
#define NM_SYS_16(op, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16) syscall((intptr_t)(op), (intptr_t)(a1), (intptr_t)(a2), (intptr_t)(a3), (intptr_t)(a4), (intptr_t)(a5), (intptr_t)(a6), (intptr_t)(a7), (intptr_t)(a8), (intptr_t)(a9), (intptr_t)(a10), (intptr_t)(a11), (intptr_t)(a12), (intptr_t)(a13), (intptr_t)(a14), (intptr_t)(a15), (intptr_t)(a16), NM_VM_END)
#define NM_SYS_PICK(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, NAME, ...) NAME
#define NM_SYSCALL(...) NM_SYS_PICK(__VA_ARGS__, NM_SYS_16, NM_SYS_15, NM_SYS_14, NM_SYS_13, NM_SYS_12, NM_SYS_11, NM_SYS_10, NM_SYS_9, NM_SYS_8, NM_SYS_7, NM_SYS_6, NM_SYS_5, NM_SYS_4, NM_SYS_3, NM_SYS_2, NM_SYS_1, NM_SYS_0)(__VA_ARGS__)
#endif
