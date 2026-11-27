/* NewMod, Arena.ai, 2026-11-21: binary team-info boundary regression.
 * Loads the actual cgame module with a fake engine, not a real network peer. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "src/cgame/cg_local.h"

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
    exit(1); } } while (0)

static intptr_t fake_syscall(intptr_t op, ...) { (void)op; return 0; }
static void *must(void *h, const char *name) {
    void *p = dlsym(h, name);
    if (!p) { fprintf(stderr, "%s: %s\n", name, dlerror()); exit(1); }
    return p;
}

int main(int argc, char **argv) {
    unsigned char packet[PACKET_OFFSET + MAX_CLIENTS * 19];
    void *h, *pages;
    void (*entry)(intptr_t (*)(intptr_t, ...));
    void (*receive)(unsigned char *, int, int);
    intptr_t (*vm)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,
                   intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
    cgs_t *state;
    long page_size;
    unsigned char *last;
    int count = MAX_CLIENTS, i;
    CHECK(argc == 2);
    h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    entry = must(h, "dllEntry"); entry(fake_syscall);
    vm = must(h, "vmMain"); receive = must(h, "CG_ReceiveTeamInfo");
    state = must(h, "cgs");

    /* Place a one-byte packet at the end of a readable page: a premature
     * four-byte header read would hit the inaccessible next page. */
    page_size = sysconf(_SC_PAGESIZE); CHECK(page_size > 0);
    pages = mmap(NULL, (size_t)page_size * 2, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(pages != MAP_FAILED);
    CHECK(mprotect((char *)pages + page_size, (size_t)page_size, PROT_NONE) == 0);
    last = (unsigned char *)pages + page_size - 1;
    *last = PACKET_S_TEAMPLAYINFOMESSAGE;
    CHECK(vm(CG_MESSAGERECEIVED, 0, 0,0,0,0,0,0,0,0,0,0,0) == -1);
    CHECK(vm(CG_MESSAGERECEIVED, (intptr_t)last, 0,0,0,0,0,0,0,0,0,0,0) == -1);
    CHECK(vm(CG_MESSAGERECEIVED, (intptr_t)last, 1,0,0,0,0,0,0,0,0,0,0) == -1);
    CHECK(vm(CG_MESSAGERECEIVED, (intptr_t)last, MAX_BINARY_MESSAGE + 1,0,0,0,0,0,0,0,0,0,0) == -1);
    receive(last, 1, 0);
    CHECK(munmap(pages, (size_t)page_size * 2) == 0);

    memset(packet, 0, sizeof(packet));
    packet[0] = PACKET_S_TEAMPLAYINFOMESSAGE;
    memcpy(packet + 1, &count, sizeof(count));
    for (i = 0; i < MAX_CLIENTS; ++i) {
        int ofs = PACKET_OFFSET + i * 19;
        int x = 1000 + i, y = -i, z = i * 3, powerups = i * 17;
        short health = (short)(i == MAX_CLIENTS - 1 ? -1 : 100 - i);
        packet[ofs] = (unsigned char)i;
        memcpy(packet + ofs + 1, &x, sizeof(x));
        memcpy(packet + ofs + 5, &y, sizeof(y));
        memcpy(packet + ofs + 9, &z, sizeof(z));
        memcpy(packet + ofs + 13, &health, sizeof(health));
        memcpy(packet + ofs + 15, &powerups, sizeof(powerups));
    }
    CHECK(vm(CG_MESSAGERECEIVED, (intptr_t)packet, sizeof(packet),0,0,0,0,0,0,0,0,0,0) == 0);
    for (i = 0; i < MAX_CLIENTS; ++i) {
        CHECK(state->clientinfo[i].location[0] == 1000 + i);
        CHECK(state->clientinfo[i].location[1] == -i);
        CHECK(state->clientinfo[i].location[2] == i * 3);
        CHECK(state->clientinfo[i].health == (i == MAX_CLIENTS - 1 ? -1 : 100 - i));
        CHECK(state->clientinfo[i].powerups == i * 17);
    }
    /* Truncated payloads, invalid counts and invalid slots must not partly
     * modify client state. */
    state->clientinfo[0].location[0] = 7777;
    CHECK(vm(CG_MESSAGERECEIVED, (intptr_t)packet, sizeof(packet) - 1,0,0,0,0,0,0,0,0,0,0) == 0);
    CHECK(state->clientinfo[0].location[0] == 7777);
    count = MAX_CLIENTS + 1; memcpy(packet + 1, &count, sizeof(count));
    CHECK(vm(CG_MESSAGERECEIVED, (intptr_t)packet, sizeof(packet),0,0,0,0,0,0,0,0,0,0) == 0);
    CHECK(state->clientinfo[0].location[0] == 7777);
    count = MAX_CLIENTS; memcpy(packet + 1, &count, sizeof(count));
    packet[PACKET_OFFSET + (MAX_CLIENTS - 1) * 19] = MAX_CLIENTS;
    CHECK(vm(CG_MESSAGERECEIVED, (intptr_t)packet, sizeof(packet),0,0,0,0,0,0,0,0,0,0) == 0);
    CHECK(state->clientinfo[0].location[0] == 7777);
    dlclose(h);
    puts("cgame team packet bounds and 64-player decode tests passed");
    return 0;
}
