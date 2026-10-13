/* Minimal diagnostic CRT using the pinned SDK's _start.S and syscall.c.
 * The stock CRT performs credential/syscall-permission patches. Those paths,
 * kernel R/W bootstrap and the dynamic loader are deliberately not linked here.
 * This tests patch-free SDK entry/syscall initialization, not the full stock CRT.
 */
#include "syscall.h"

extern unsigned char __bss_start[], __bss_end[];
extern unsigned int diagnostic_firmware;
extern int main(void);

int __crt_start(payload_args_t *args)
{
    for (unsigned char *p = __bss_start; p < __bss_end; ++p) *p = 0;
    if (!args || !args->payloadout || !args->sys_dynlib_dlsym) return -1;
    int result = __crt_syscall_init(args);
    if (!result) {
        const int mib[2] = {1, 46};
        unsigned long length = sizeof(diagnostic_firmware);
        if (__crt_syscall(SYS_sysctl, mib, 2, &diagnostic_firmware, &length, 0, 0) ||
            length != sizeof(diagnostic_firmware))
            result = 2;
        else
            result = main();
        *args->payloadout = result;
        /* elfldr launched a disposable child; leave no process behind. */
        (void)__crt_syscall(1 /* SYS_exit */, result);
    }
    *args->payloadout = result;
    return result;
}
