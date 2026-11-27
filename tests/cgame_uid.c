/* NewMod, Arena.ai, 2026-11-21: exercise the optional inherited NQ-key
 * client against a fake engine. This is not an authentication test. */
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/cgame/cg_local.h"

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
    exit(1); } } while (0)
#define VM_END ((intptr_t)-1337)
#define KEY_LENGTH 28

static unsigned char keyfile[KEY_LENGTH + 1];
static unsigned char packet[PACKET_OFFSET + PB_GUID_LENGTH];
static char console[2048];
static int key_length = -1, deny_write, short_read, writes, sends;

static intptr_t fake_syscall(intptr_t op, ...) {
    intptr_t a[8], value;
    int n = 0;
    va_list ap;
    va_start(ap, op);
    while ((value = va_arg(ap, intptr_t)) != VM_END) {
        CHECK(n < (int)(sizeof(a) / sizeof(a[0])));
        a[n++] = value;
    }
    va_end(ap);
    switch (op) {
    case CG_FS_FOPENFILE:
        CHECK(n == 3 && !strcmp((char *)a[0], "nqkey.cfg"));
        if (a[2] == FS_READ) {
            *(fileHandle_t *)a[1] = key_length < 0 ? 0 : 1;
            return key_length;
        }
        CHECK(a[2] == FS_WRITE);
        *(fileHandle_t *)a[1] = deny_write ? 0 : 2;
        return deny_write ? -1 : 0;
    case CG_FS_READ:
        CHECK(n == 3 && a[1] == KEY_LENGTH && a[2] == 1);
        memcpy((void *)a[0], keyfile, short_read ? KEY_LENGTH / 2 : KEY_LENGTH);
        return 0;
    case CG_FS_WRITE:
        CHECK(n == 3 && a[1] == KEY_LENGTH && a[2] == 2);
        memcpy(keyfile, (void *)a[0], KEY_LENGTH);
        keyfile[KEY_LENGTH] = 0;
        key_length = KEY_LENGTH; ++writes;
        return KEY_LENGTH;
    case CG_FS_FCLOSEFILE:
        CHECK(n == 1 && (a[0] == 1 || a[0] == 2));
        return 0;
    case CG_PRINT:
        CHECK(n == 1);
        strncat(console, (char *)a[0], sizeof(console) - strlen(console) - 1);
        return 0;
    case CG_SENDMESSAGE:
        CHECK(n == 2 && a[1] == sizeof(packet));
        memcpy(packet, (void *)a[0], sizeof(packet));
        ++sends;
        return 0;
    default:
        fprintf(stderr, "unexpected cgame syscall %ld\n", (long)op);
        exit(1);
    }
}

static void *must(void *h, const char *name) {
    void *p = dlsym(h, name);
    if (!p) { fprintf(stderr, "%s: %s\n", name, dlerror()); exit(1); }
    return p;
}

static void check_packet(char *(*hash)(const char *)) {
    int i;
    char expected[PB_GUID_LENGTH + 1];
    CHECK(key_length == KEY_LENGTH && strlen((char *)keyfile) == KEY_LENGTH);
    for (i = 0; i < KEY_LENGTH; ++i) CHECK(keyfile[i] >= '0' && keyfile[i] <= '9');
    snprintf(expected, sizeof(expected), "%s", hash((char *)keyfile));
    CHECK(packet[0] == PACKET_C_SEND_NQKEY);
    for (i = 1; i < PACKET_OFFSET; ++i) CHECK(packet[i] == 0);
    CHECK(!memcmp(packet + PACKET_OFFSET, expected, PB_GUID_LENGTH));
    CHECK(!strstr(console, (char *)keyfile) && !strstr(console, expected));
}

int main(int argc, char **argv) {
    void *h;
    void (*entry)(intptr_t (*)(intptr_t, ...)), (*send_key)(void);
    char *(*hash)(const char *);
    unsigned char first[sizeof(packet)];
    CHECK(argc == 2);
    h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    entry = must(h, "dllEntry"); entry(fake_syscall);
    send_key = must(h, "CG_InitNQGUID"); hash = must(h, "CalculateGUID");

    /* Old 28-byte key is read, hashed and sent unchanged. */
    memcpy(keyfile, "0000001002202611210012345678", KEY_LENGTH + 1);
    key_length = KEY_LENGTH;
    send_key(); CHECK(sends == 1 && writes == 0); check_packet(hash);

    /* Missing key is created in the old format, then remains stable. */
    key_length = -1; send_key(); CHECK(sends == 2 && writes == 1);
    CHECK(!memcmp(keyfile, "000000100220", 12)); check_packet(hash);
    memcpy(first, packet, sizeof(packet));
    send_key(); CHECK(sends == 3 && writes == 1);
    CHECK(!memcmp(packet, first, sizeof(packet))); check_packet(hash);

    /* Invalid/short key data is replaced, not hashed as stale memory. */
    keyfile[3] = 'x'; send_key(); CHECK(sends == 4 && writes == 2); check_packet(hash);
    short_read = 1; send_key(); CHECK(sends == 5 && writes == 3); check_packet(hash);
    short_read = 0;

    /* A write failure must not broadcast the shared all-ones fallback. */
    key_length = -1; deny_write = 1;
    send_key(); CHECK(sends == 5 && writes == 3);
    CHECK(strstr(console, "could not write"));
    dlclose(h);
    puts("optional NQ key packet and file-boundary tests passed");
    return 0;
}
