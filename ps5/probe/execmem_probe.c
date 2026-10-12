/* Native PS5 capability probe for the OpenGOAL port.
 *
 * OpenGOAL loads compiled GOAL code into its EE main memory and runs it there,
 * so that memory must be readable, writable and executable through one mapping
 * (game/runtime.cpp, ee_runner). This asks the console, without guessing, which
 * of the available interfaces give a native title such memory.
 *
 * Nothing here executes from a page the kernel does not report as executable,
 * and a fault handler covers the case where the report is wrong, so a refusal
 * is a log line rather than a crashed title.
 *
 * Linked with --wrap=main in front of the hardware-proven SDL3 example, which
 * runs unchanged afterwards. */
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* common/goal_constants.h */
#define EE_MAIN_MEM_MAP 0x2123000000ull
#define EE_MAIN_MEM_SIZE (128u << 20)
#define EE_MAIN_MEM_LOW_PROTECT (512u * 1024u)

#define SMALL (64u * 1024u)
#define RW (PROT_READ | PROT_WRITE)
#define RX (PROT_READ | PROT_EXEC)
#define RWX (PROT_READ | PROT_WRITE | PROT_EXEC)
#define LOG_DIR "/data/opengoal"
#define LOG_FILE LOG_DIR "/probe.log"

int sceKernelQueryMemoryProtection(void* address, void** start, void** end, int* protection);
int sceKernelMprotect(const void* address, size_t size, int protection);
int sceKernelMapNamedFlexibleMemory(void** address, size_t size, int protection, int flags,
                                    const char* name);
int sceKernelJitCreateSharedMemory(const char* name, size_t size, int max_protection, int* fd);
int sceKernelJitCreateAliasOfSharedMemory(int fd, int max_protection, int* alias_fd);
int sceKernelJitMapSharedMemory(int fd, int protection, void** address);
int sceKernelAvailableFlexibleMemorySize(size_t* size);
size_t sceKernelGetDirectMemorySize(void);

int __real_main(int argc, char** argv);
void og_runtime_probe(void (*say)(const char* format, ...));
void og_exception_probe(void (*say)(const char* format, ...));

static FILE* g_log;

static void say(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
  printf("\n");
  if (g_log) {
    va_start(args, format);
    vfprintf(g_log, format, args);
    va_end(args);
    fputc('\n', g_log);
    fflush(g_log);
  }
}

static sigjmp_buf g_fault_jump;
static volatile sig_atomic_t g_fault_armed;
static volatile sig_atomic_t g_fault_signal;

static void on_fault(int signal_number) {
  if (g_fault_armed) {
    g_fault_armed = 0;
    g_fault_signal = signal_number;
    siglongjmp(g_fault_jump, 1);
  }
  signal(signal_number, SIG_DFL);
}

/* The protection the kernel reports for an address, or -1. */
static int reported(void* address, int* query_result) {
  void* start = NULL;
  void* end = NULL;
  int protection = 0;
  int result = sceKernelQueryMemoryProtection(address, &start, &end, &protection);
  if (query_result)
    *query_result = result;
  return result == 0 ? protection : -1;
}

/* Write a function through one view and call it through another (or the same). */
static void try_exec(const char* test, void* write_view, void* exec_view) {
  /* mov eax, 0x1234 ; ret */
  static const uint8_t code[] = {0xb8, 0x34, 0x12, 0x00, 0x00, 0xc3};
  int query = 0;
  int write_protection = reported(write_view, &query);
  int exec_protection = reported(exec_view, &query);
  if (write_protection < 0 || exec_protection < 0) {
    say("[probe] %s exec=not-attempted reason=query-failed query=0x%x", test, (unsigned)query);
    return;
  }
  if ((write_protection & RW) != RW || (exec_protection & RX) != RX) {
    say("[probe] %s exec=not-attempted reason=reported-protection write_view=0x%x exec_view=0x%x",
        test, (unsigned)write_protection, (unsigned)exec_protection);
    return;
  }
  g_fault_signal = 0;
  if (sigsetjmp(g_fault_jump, 1) == 0) {
    g_fault_armed = 1;
    memcpy(write_view, code, sizeof(code));
    int value = ((int (*)(void))exec_view)();
    g_fault_armed = 0;
    say("[probe] %s exec=%s returned=0x%x write_view=0x%x exec_view=0x%x", test,
        value == 0x1234 ? "ok" : "wrong-result", (unsigned)value, (unsigned)write_protection,
        (unsigned)exec_protection);
  } else {
    say("[probe] %s exec=fault signal=%d write_view=0x%x exec_view=0x%x", test,
        (int)g_fault_signal, (unsigned)write_protection, (unsigned)exec_protection);
  }
}

static void* map_anonymous(const char* test, void* hint, size_t size, int protection, int fd) {
  errno = 0;
  void* address = mmap(hint, size, protection, MAP_PRIVATE | MAP_ANON, fd, 0);
  int error = errno;
  if (address == MAP_FAILED) {
    say("[probe] %s mmap=failed errno=%d (%s) size=0x%zx prot=0x%x fd=%d", test, error,
        strerror(error), size, (unsigned)protection, fd);
    return NULL;
  }
  say("[probe] %s mmap=ok address=%p size=0x%zx prot=0x%x fd=%d reported=0x%x", test, address, size,
      (unsigned)protection, fd, (unsigned)reported(address, NULL));
  return address;
}

static void test_mmap(void) {
  void* address = map_anonymous("mmap-rwx", NULL, SMALL, RWX, -1);
  if (address) {
    try_exec("mmap-rwx", address, address);
    munmap(address, SMALL);
  }

  address = map_anonymous("mprotect-rw-to-rwx", NULL, SMALL, RW, -1);
  if (address) {
    errno = 0;
    int result = mprotect(address, SMALL, RWX);
    say("[probe] mprotect-rw-to-rwx mprotect=%d errno=%d reported=0x%x", result, errno,
        (unsigned)reported(address, NULL));
    try_exec("mprotect-rw-to-rwx", address, address);
    munmap(address, SMALL);
  }

  address = map_anonymous("sce-mprotect-rw-to-rwx", NULL, SMALL, RW, -1);
  if (address) {
    int result = sceKernelMprotect(address, SMALL, RWX);
    say("[probe] sce-mprotect-rw-to-rwx sceKernelMprotect=0x%x reported=0x%x", (unsigned)result,
        (unsigned)reported(address, NULL));
    try_exec("sce-mprotect-rw-to-rwx", address, address);
    munmap(address, SMALL);
  }

  /* Write, then flip the same pages to read+execute: not enough for OpenGOAL
   * as it stands, but it says whether any executable anonymous page exists. */
  address = map_anonymous("mprotect-rw-then-rx", NULL, SMALL, RW, -1);
  if (address) {
    static const uint8_t code[] = {0xb8, 0x34, 0x12, 0x00, 0x00, 0xc3};
    memcpy(address, code, sizeof(code));
    errno = 0;
    int result = mprotect(address, SMALL, RX);
    int protection = reported(address, NULL);
    say("[probe] mprotect-rw-then-rx mprotect=%d errno=%d reported=0x%x", result, errno,
        (unsigned)protection);
    if (result == 0 && protection >= 0 && (protection & RX) == RX) {
      g_fault_signal = 0;
      if (sigsetjmp(g_fault_jump, 1) == 0) {
        g_fault_armed = 1;
        int value = ((int (*)(void))address)();
        g_fault_armed = 0;
        say("[probe] mprotect-rw-then-rx exec=%s returned=0x%x",
            value == 0x1234 ? "ok" : "wrong-result", (unsigned)value);
      } else {
        say("[probe] mprotect-rw-then-rx exec=fault signal=%d", (int)g_fault_signal);
      }
    } else {
      say("[probe] mprotect-rw-then-rx exec=not-attempted reason=reported-protection");
    }
    munmap(address, SMALL);
  }
}

static void test_flexible(void) {
  void* address = NULL;
  int result = sceKernelMapNamedFlexibleMemory(&address, SMALL, RWX, 0, "og-probe");
  say("[probe] flexible-rwx sceKernelMapNamedFlexibleMemory=0x%x address=%p reported=0x%x",
      (unsigned)result, address, result == 0 ? (unsigned)reported(address, NULL) : 0u);
  if (result == 0 && address) {
    try_exec("flexible-rwx", address, address);
    munmap(address, SMALL);
  }
}

static void test_jit(void) {
  int fd = -1;
  int result = sceKernelJitCreateSharedMemory("og-probe", SMALL, RWX, &fd);
  say("[probe] jit-shared create=0x%x fd=%d", (unsigned)result, fd);
  if (result != 0 || fd < 0)
    return;

  void* both = NULL;
  result = sceKernelJitMapSharedMemory(fd, RWX, &both);
  say("[probe] jit-shared-rwx map=0x%x address=%p reported=0x%x", (unsigned)result, both,
      result == 0 ? (unsigned)reported(both, NULL) : 0u);
  if (result == 0 && both)
    try_exec("jit-shared-rwx", both, both);

  /* The interface's intended shape: one writable view and one executable view. */
  void* writable = NULL;
  result = sceKernelJitMapSharedMemory(fd, RW, &writable);
  say("[probe] jit-alias map-rw=0x%x address=%p reported=0x%x", (unsigned)result, writable,
      result == 0 ? (unsigned)reported(writable, NULL) : 0u);
  int alias = -1;
  int alias_result = sceKernelJitCreateAliasOfSharedMemory(fd, RX, &alias);
  say("[probe] jit-alias create-alias=0x%x fd=%d", (unsigned)alias_result, alias);
  if (result == 0 && writable && alias_result == 0 && alias >= 0) {
    void* executable = NULL;
    result = sceKernelJitMapSharedMemory(alias, RX, &executable);
    say("[probe] jit-alias map-rx=0x%x address=%p reported=0x%x", (unsigned)result, executable,
        result == 0 ? (unsigned)reported(executable, NULL) : 0u);
    if (result == 0 && executable) {
      try_exec("jit-alias", writable, executable);
      munmap(executable, SMALL);
    }
  }
  if (writable)
    munmap(writable, SMALL);
  if (both)
    munmap(both, SMALL);
  if (alias >= 0)
    close(alias);
  close(fd);
}

/* The mapping game/runtime.cpp makes, at its size and address hint. */
static void test_ee_main_memory(void) {
  /* runtime.cpp passes fd 0 with MAP_ANONYMOUS; FreeBSD documents -1. */
  void* address = map_anonymous("ee-fd0", NULL, SMALL, RW, 0);
  if (address)
    munmap(address, SMALL);

  const char* test = "ee-main-rwx";
  address = map_anonymous(test, (void*)EE_MAIN_MEM_MAP, EE_MAIN_MEM_SIZE, RWX, -1);
  if (!address) {
    test = "ee-main-rw";
    address = map_anonymous(test, (void*)EE_MAIN_MEM_MAP, EE_MAIN_MEM_SIZE, RW, -1);
  }
  if (!address)
    return;
  say("[probe] %s hint=%s", test, address == (void*)EE_MAIN_MEM_MAP ? "honored" : "ignored");
  if ((reported(address, NULL) & RW) == RW) {
    g_fault_signal = 0;
    if (sigsetjmp(g_fault_jump, 1) == 0) {
      g_fault_armed = 1;
      memset(address, 0, EE_MAIN_MEM_SIZE);
      g_fault_armed = 0;
      say("[probe] %s commit=ok bytes=0x%x", test, EE_MAIN_MEM_SIZE);
    } else {
      say("[probe] %s commit=fault signal=%d", test, (int)g_fault_signal);
    }
  }
  errno = 0;
  int result = mprotect(address, EE_MAIN_MEM_LOW_PROTECT, PROT_NONE);
  say("[probe] %s low-protect mprotect=%d errno=%d reported=0x%x", test, result, errno,
      (unsigned)reported(address, NULL));
  uint8_t* above = (uint8_t*)address + (1u << 20);
  try_exec(test, above, above);
  munmap(address, EE_MAIN_MEM_SIZE);
}

static void open_log(void) {
  errno = 0;
  int made = mkdir(LOG_DIR, 0777);
  int made_error = errno;
  errno = 0;
  g_log = fopen(LOG_FILE, "w");
  int open_error = errno;
  say("[probe] log mkdir=%d errno=%d fopen=%s errno=%d path=%s", made, made_error,
      g_log ? "ok" : "failed", open_error, LOG_FILE);
}

int __wrap_main(int argc, char** argv) {
  open_log();
  say("[probe] start pid=%d uid=%d pagesize=%d", (int)getpid(), (int)getuid(), getpagesize());

  size_t flexible = 0;
  int result = sceKernelAvailableFlexibleMemorySize(&flexible);
  say("[probe] memory flexible-available=0x%zx (query=0x%x) direct-total=0x%zx", flexible,
      (unsigned)result, sceKernelGetDirectMemorySize());

  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = on_fault;
  sigemptyset(&action.sa_mask);
  int handlers = sigaction(SIGSEGV, &action, NULL) | sigaction(SIGBUS, &action, NULL) |
                 sigaction(SIGILL, &action, NULL);
  say("[probe] fault-handlers sigaction=%d errno=%d", handlers, handlers ? errno : 0);

  /* Ordered so that a step which could end the title comes after everything
   * that cannot: the full-size commit, then the exception. */
  test_mmap();
  test_flexible();
  test_jit();
  og_runtime_probe(say);
  test_ee_main_memory();

  signal(SIGSEGV, SIG_DFL);
  signal(SIGBUS, SIG_DFL);
  signal(SIGILL, SIG_DFL);
  say("[probe] execmem complete");

  og_exception_probe(say);
  say("[probe] complete");
  if (g_log)
    fflush(g_log);
  return __real_main(argc, argv);
}
