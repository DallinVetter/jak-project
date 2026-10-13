#!/usr/bin/env python3
"""Host-only failure-injection tests for the patched credential functions and stdio.

Usage: python3 ps5/probe/test_fw1342.py build/ps5/fw1342-001/helper/source
Compiles the actual patched functions against mock kernel accessors. No PS5 ELF runs.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent


def compile_c(folder, text, name, *extra):
    source = folder / (name + ".c")
    source.write_text(text)
    binary = folder / name
    subprocess.run(["cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra",
                    "-Werror", "-I" + str(folder), str(source), *extra, "-o", str(binary)], check=True)
    return binary


def credential_harness(source):
    struct = source[source.index("struct credentials {"):source.index("static int read_ptr")]
    functions = source[source.index("static int save_credentials"):source.index("static int await_target_stop")]
    elevated = source[source.index("    elevated = original;"):source.index("    cred_changed = 1;")]
    prefix = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#define UCRED_NGROUPS 0x10u
#define SYSTEM_AUTHID UINT64_C(0x4801000000000013)
'''
    mocks = r'''
static struct credentials live;
static int failure, set_calls;
#define ACCESS(field, id, type) \
static type kernel_get_ucred_##field(pid_t pid) { \
    (void)pid; return failure == id ? (type)-1 : live.field; } \
static int kernel_set_ucred_##field(pid_t pid, type value) { \
    (void)pid; ++set_calls; if (failure == id + 20) return -1; live.field = value; return 0; }
ACCESS(uid, 1, uid_t)
ACCESS(ruid, 2, uid_t)
ACCESS(svuid, 3, uid_t)
ACCESS(rgid, 4, gid_t)
ACCESS(svgid, 5, gid_t)
static uint64_t kernel_get_ucred_authid(pid_t pid) {
    (void)pid; return failure == 6 ? 0 : live.authid;
}
static int kernel_set_ucred_authid(pid_t pid, uint64_t value) {
    (void)pid; ++set_calls; if (failure == 26) return -1; live.authid = value; return 0;
}
#define BUFFER(field, id, size) \
static int kernel_get_ucred_##field(pid_t pid, unsigned char out[size]) { \
    (void)pid; if (failure == id) return -1; memcpy(out, live.field, size); return 0; } \
static int kernel_set_ucred_##field(pid_t pid, const unsigned char in[size]) { \
    (void)pid; ++set_calls; if (failure == id + 20) return -1; memcpy(live.field, in, size); return 0; }
BUFFER(attrs, 7, 32)
BUFFER(caps, 8, 16)
static int kernel_copyout(intptr_t addr, void *out, size_t size) {
    assert(addr == 0x1000 + UCRED_NGROUPS && size == sizeof(live.ngroups));
    if (failure == 9) return -1;
    memcpy(out, &live.ngroups, size); return 0;
}
static int kernel_copyin(const void *in, intptr_t addr, size_t size) {
    assert(addr == 0x1000 + UCRED_NGROUPS && size == sizeof(live.ngroups));
    ++set_calls; if (failure == 29) return -1;
    memcpy(&live.ngroups, in, size); return 0;
}
'''
    tests = r'''
int main(void) {
    struct credentials original = {.uid=1, .ruid=2, .svuid=3, .rgid=4, .svgid=5,
                                   .ngroups=6, .authid=7};
    for (unsigned i=0; i<32; ++i) original.attrs[i] = (uint8_t)(i + 1);
    for (unsigned i=0; i<16; ++i) original.caps[i] = (uint8_t)(i + 40);
    live = original;
    struct credentials found, elevated;
    assert(save_credentials(2, 0x1000, &found) == 0);
    assert(!memcmp(found.attrs, original.attrs, 32));
    assert(credentials_match(2, &original, 0x1000));
    live.attrs[31] ^= 1;
    assert(!credentials_match(2, &original, 0x1000));
    live = original;
'''
    ending = r'''
    for (unsigned i=0; i<32; ++i)
        assert(elevated.attrs[i] == (uint8_t)(original.attrs[i] | (i == 3 ? 0x80 : 0)));
    assert(set_credentials(2, 0x1000, &elevated) == 0);
    assert(credentials_match(2, &elevated, 0x1000));
    assert(set_credentials(2, 0x1000, &original) == 0);
    assert(credentials_match(2, &original, 0x1000));
    for (failure=1; failure<=9; ++failure) {
        set_calls = 0;
        assert(save_credentials(2, 0x1000, &found) == EFAULT);
        assert(!credentials_match(2, &original, 0x1000));
        assert(set_calls == 0);
    }
    for (int id=1; id<=9; ++id) {
        live = original; failure = id;
        assert(set_credentials(2, 0x1000, &elevated) == EIO); /* read-back faults fail closed */
        failure = 0;
        assert(set_credentials(2, 0x1000, &original) == 0);
    }
    for (int id=21; id<=29; ++id) {
        live = original; failure = id; set_calls = 0;
        assert(set_credentials(2, 0x1000, &elevated) == EIO);
        assert(set_calls == 9); /* every setter's result contributes */
        failure = 0;
        assert(set_credentials(2, 0x1000, &original) == 0);
        assert(credentials_match(2, &original, 0x1000));
    }
    return 0;
}
'''
    return prefix + struct + mocks + functions + tests + elevated + ending


STDIO_SHIM = r'''
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <sys/syscall.h>
extern unsigned int diagnostic_firmware;
extern int stdio_main(void);
long __crt_syscall(long number, ...) {
    va_list ap; va_start(ap, number); long result = -1;
    if (number == SYS_open) result = -1; /* host tests never touch /data */
    else if (number == SYS_read || number == SYS_write) {
        int fd = va_arg(ap, int); void *bytes = va_arg(ap, void *);
        size_t size = va_arg(ap, size_t);
        if (size > 3) size = 3; /* force partial transfers in both directions */
        result = number == SYS_read ? read(fd, bytes, size) : write(fd, bytes, size);
    } else if (number == SYS_poll) {
        struct pollfd *fds = va_arg(ap, struct pollfd *);
        (void)va_arg(ap, int); int timeout = va_arg(ap, int);
        result = getenv("FORCE_TIMEOUT") ? 0 : poll(fds, 1, timeout);
    } else if (number == SYS_clock_gettime) {
        int clock = va_arg(ap, int); struct timespec *now = va_arg(ap, struct timespec *);
        result = clock_gettime(clock, now);
    }
    va_end(ap); return result;
}
int main(void) {
    diagnostic_firmware = getenv("WRONG_FIRMWARE") ? 0x13200000 : 0x13420000;
    return stdio_main();
}
'''


class Checks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="fw1342-host-tests-")
        cls.folder = Path(cls.temp.name)
        source = (LAPY / "source/owned_root_daemon.c").read_text()
        cls.credentials = compile_c(cls.folder, credential_harness(source), "credentials")
        (cls.folder / "syscall.h").write_text("long __crt_syscall(long, ...);\n")
        (cls.folder / "stdio_identity.h").write_text('#define STDIO_BUILD_ID "host-test"\n#define STDIO_SDK_REVISION "host-test"\n')
        obj = cls.folder / "stdio.o"
        subprocess.run(["cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Dmain=stdio_main",
                        "-Wall", "-Wextra", "-Werror", "-I" + str(cls.folder), "-c",
                        str(HERE / "elfldr_stdio.c"), "-o", str(obj)], check=True)
        cls.stdio = compile_c(cls.folder, STDIO_SHIM, "stdio", str(obj))

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_credentials_full_save_compare_restore_and_api_failures(self):
        subprocess.run([str(self.credentials)], check=True)

    def diagnostic(self, data, **environment):
        import os
        return subprocess.run([str(self.stdio)], input=data, capture_output=True, timeout=6,
                              env={**os.environ, **environment})

    def test_stdio_partial_read_write(self):
        result = self.diagnostic(b"OGSTDIO-REQ-v1!\n")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"OGSTDIO-ACK-v1!\n")
        self.assertEqual(result.stderr, b"")

    def test_stdio_invalid_request_has_no_reply(self):
        result = self.diagnostic(b"OGSTDIO-BAD-v1!\n")
        self.assertEqual(result.returncode, 4)
        self.assertEqual(result.stdout, b"")

    def test_stdio_short_request_has_no_reply(self):
        result = self.diagnostic(b"short")
        self.assertEqual(result.returncode, 3)
        self.assertEqual(result.stdout, b"")

    def test_stdio_timeout_has_no_reply(self):
        result = self.diagnostic(b"OGSTDIO-REQ-v1!\n", FORCE_TIMEOUT="1")
        self.assertEqual(result.returncode, 3)
        self.assertEqual(result.stdout, b"")

    def test_stdio_wrong_firmware_has_no_reply(self):
        result = self.diagnostic(b"OGSTDIO-REQ-v1!\n", WRONG_FIRMWARE="1")
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, b"")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("lapy_source", type=Path)
    LAPY = parser.parse_args().lapy_source
    unittest.main(argv=[__file__], verbosity=2)
