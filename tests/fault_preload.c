/* Test-only glibc/Linux fault injection. This library is preloaded into the
 * fake-engine process, never linked into or shipped with the game modules. */
#define _GNU_SOURCE
#include <errno.h>
#include <stddef.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t, size_t);
static int fail_clock, malloc_after = -1, calloc_after = -1;

void NM_test_fail_clock(int enabled) { fail_clock = enabled; }
void NM_test_fail_malloc_after(int count) { malloc_after = count; }
void NM_test_fail_calloc_after(int count) { calloc_after = count; }

void *malloc(size_t size) {
    if (malloc_after == 0) { malloc_after = -1; errno = ENOMEM; return NULL; }
    if (malloc_after > 0) --malloc_after;
    return __libc_malloc(size);
}

void *calloc(size_t count, size_t size) {
    if (calloc_after == 0) { calloc_after = -1; errno = ENOMEM; return NULL; }
    if (calloc_after > 0) --calloc_after;
    return __libc_calloc(count, size);
}

time_t time(time_t *result) {
    if (fail_clock) {
        if (result) *result = (time_t)-1;
        errno = EIO;
        return (time_t)-1;
    }
    return (time_t)syscall(SYS_time, result);
}
