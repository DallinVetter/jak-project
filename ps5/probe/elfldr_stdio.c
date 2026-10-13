/* Harmless elfldr stdio diagnostic. No libc, credential or kernel-write APIs.
 * stdin/stdout/stderr may alias one socket: logs go only to the separate file.
 * Wire request: OGSTDIO-REQ-v1!\n (16 bytes); reply: OGSTDIO-ACK-v1!\n.
 */
#include <stddef.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <time.h>
#include <sys/syscall.h>
#include "syscall.h"
#include "stdio_identity.h"

unsigned int diagnostic_firmware;

static void log_line(const char *line, size_t length)
{
    const long fd = __crt_syscall(SYS_open, "/data/opengoal/elfldr_stdio.log",
                                 O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    while (length) {
        const long count = __crt_syscall(SYS_write, fd, line, length);
        if (count <= 0 || (size_t)count > length) break;
        line += count;
        length -= (size_t)count;
    }
    (void)__crt_syscall(SYS_close, fd);
}

#define LOG(message) log_line(message "\n", sizeof(message "\n") - 1)

static int64_t monotonic_ms(void)
{
    struct timespec now;
    if (__crt_syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &now)) return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int transfer(int fd, unsigned char *bytes, size_t size, int writing,
                    int64_t deadline)
{
    while (size) {
        const int64_t now = monotonic_ms();
        if (now < 0 || now >= deadline) return 1;
        struct pollfd ready = {fd, writing ? POLLOUT : POLLIN, 0};
        if (__crt_syscall(SYS_poll, &ready, 1, (int)(deadline - now)) <= 0 ||
            !(ready.revents & ready.events)) return 1;
        const long count = __crt_syscall(writing ? SYS_write : SYS_read, fd, bytes, size);
        if (count <= 0 || (size_t)count > size) return 1;
        bytes += count;
        size -= (size_t)count;
    }
    return 0;
}

int main(void)
{
    static const unsigned char request[] = "OGSTDIO-REQ-v1!\n";
    unsigned char reply[] = "OGSTDIO-ACK-v1!\n";
    unsigned char received[sizeof(request) - 1];
    LOG("stdio main build=" STDIO_BUILD_ID);
    if ((diagnostic_firmware & 0xffff0000u) != 0x13420000u) {
        LOG("stdio firmware mismatch");
        return 2;
    }
    LOG("stdio firmware=13.42 sdk=" STDIO_SDK_REVISION);
    const int64_t started = monotonic_ms();
    if (started < 0 || transfer(0, received, sizeof(received), 0, started + 5000)) {
        LOG("stdio request read failed");
        return 3;
    }
    for (size_t i = 0; i < sizeof(received); ++i) {
        if (received[i] != request[i]) {
            LOG("stdio request invalid");
            return 4;
        }
    }
    LOG("stdio request bytes=16 valid=1");
    if (transfer(1, reply, sizeof(reply) - 1, 1, started + 5000)) {
        LOG("stdio reply write failed");
        return 5;
    }
    LOG("stdio reply bytes=16 success=1");
    return 0;
}
