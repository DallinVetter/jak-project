/* Bounded elevated executable-memory probe for the OpenGOAL PS5 port.
 *
 * OpenGOAL runs compiled GOAL code from its EE main memory, so that memory must be
 * readable, writable and executable through one mapping (game/runtime.cpp). A sandboxed
 * native title is granted read/write only (ps5/AUDIT.md, probe results
 * PPSA99005-20261012T215826Z). This answers one question for the owner's option 1: does the
 * existing, already-qualified title-scoped elevation change that?
 *
 * The experiment is a single bounded hardware run:
 *   1. run the executable-memory capability checks  (phase "pre")
 *   2. request the existing title-scoped elevation   (og_title_elevate, see title_elevate.cpp)
 *   3. run exactly the same checks                    (phase "post")
 *   4. log return values, errno, the protection the kernel reports, and whether execute works
 *   5. exit cleanly
 *
 * Nothing here executes from a page the kernel does not report as executable, and a fault
 * handler covers a wrong report, so a refusal is a log line rather than a crashed title. This
 * is a standalone experiment: it provides main() and does not run the renderer. It does not
 * touch the OpenGOAL runtime.
 *
 * The capability checks mirror ps5/probe/execmem_probe.c (the proven probe), parameterized by
 * a phase label so the pre/post passes are directly comparable. */
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

#define SMALL (64u * 1024u)
#define RW (PROT_READ | PROT_WRITE)
#define RX (PROT_READ | PROT_EXEC)
#define RWX (PROT_READ | PROT_WRITE | PROT_EXEC)
#define LOG_DIR "/data/opengoal"
#define LOG_FILE LOG_DIR "/elevated_probe.log"

int sceKernelQueryMemoryProtection(void* address, void** start, void** end, int* protection);
int sceKernelMprotect(const void* address, size_t size, int protection);
int sceKernelMapNamedFlexibleMemory(void** address, size_t size, int protection, int flags,
                                    const char* name);
int sceKernelJitCreateSharedMemory(const char* name, size_t size, int max_protection, int* fd);
int sceKernelJitCreateAliasOfSharedMemory(int fd, int max_protection, int* alias_fd);
int sceKernelJitMapSharedMemory(int fd, int protection, void** address);

/* The title-scoped elevation client (title_elevate.cpp -> boilerplate elevation::request). */
int og_title_elevate(void);
const char* og_title_elevate_path(void);

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

/* Does executing actually work? Writes a tiny function through write_view and calls it through
 * exec_view, but only when the kernel reports the pages writable and executable. */
static void try_exec(const char* phase, const char* test, void* write_view, void* exec_view) {
  /* mov eax, 0x1234 ; ret */
  static const uint8_t code[] = {0xb8, 0x34, 0x12, 0x00, 0x00, 0xc3};
  int query = 0;
  int write_protection = reported(write_view, &query);
  int exec_protection = reported(exec_view, &query);
  if (write_protection < 0 || exec_protection < 0) {
    say("[probe] %s %s exec=not-attempted reason=query-failed query=0x%x", phase, test,
        (unsigned)query);
    return;
  }
  if ((write_protection & RW) != RW || (exec_protection & RX) != RX) {
    say("[probe] %s %s exec=not-attempted reason=reported-protection write_view=0x%x "
        "exec_view=0x%x execute-present=no",
        phase, test, (unsigned)write_protection, (unsigned)exec_protection);
    return;
  }
  g_fault_signal = 0;
  if (sigsetjmp(g_fault_jump, 1) == 0) {
    g_fault_armed = 1;
    memcpy(write_view, code, sizeof(code));
    int value = ((int (*)(void))exec_view)();
    g_fault_armed = 0;
    say("[probe] %s %s exec=%s returned=0x%x write_view=0x%x exec_view=0x%x execute-present=yes",
        phase, test, value == 0x1234 ? "ok" : "wrong-result", (unsigned)value,
        (unsigned)write_protection, (unsigned)exec_protection);
  } else {
    say("[probe] %s %s exec=fault signal=%d write_view=0x%x exec_view=0x%x execute-present=no",
        phase, test, (int)g_fault_signal, (unsigned)write_protection, (unsigned)exec_protection);
  }
}

static void* map_anonymous(const char* phase, const char* test, void* hint, size_t size,
                           int protection, int fd) {
  errno = 0;
  void* address = mmap(hint, size, protection, MAP_PRIVATE | MAP_ANON, fd, 0);
  int error = errno;
  if (address == MAP_FAILED) {
    say("[probe] %s %s mmap=failed errno=%d (%s) size=0x%zx prot=0x%x fd=%d", phase, test, error,
        strerror(error), size, (unsigned)protection, fd);
    return NULL;
  }
  say("[probe] %s %s mmap=ok address=%p size=0x%zx prot=0x%x fd=%d reported=0x%x", phase, test,
      address, size, (unsigned)protection, fd, (unsigned)reported(address, NULL));
  return address;
}

static void test_mmap(const char* phase) {
  void* address = map_anonymous(phase, "mmap-rwx", NULL, SMALL, RWX, -1);
  if (address) {
    try_exec(phase, "mmap-rwx", address, address);
    munmap(address, SMALL);
  }

  address = map_anonymous(phase, "mprotect-rw-to-rwx", NULL, SMALL, RW, -1);
  if (address) {
    errno = 0;
    int result = mprotect(address, SMALL, RWX);
    say("[probe] %s mprotect-rw-to-rwx mprotect=%d errno=%d reported=0x%x", phase, result, errno,
        (unsigned)reported(address, NULL));
    try_exec(phase, "mprotect-rw-to-rwx", address, address);
    munmap(address, SMALL);
  }

  address = map_anonymous(phase, "sce-mprotect-rw-to-rwx", NULL, SMALL, RW, -1);
  if (address) {
    int result = sceKernelMprotect(address, SMALL, RWX);
    say("[probe] %s sce-mprotect-rw-to-rwx sceKernelMprotect=0x%x reported=0x%x", phase,
        (unsigned)result, (unsigned)reported(address, NULL));
    try_exec(phase, "sce-mprotect-rw-to-rwx", address, address);
    munmap(address, SMALL);
  }
}

static void test_flexible(const char* phase) {
  void* address = NULL;
  int result = sceKernelMapNamedFlexibleMemory(&address, SMALL, RWX, 0, "og-probe");
  say("[probe] %s flexible-rwx sceKernelMapNamedFlexibleMemory=0x%x address=%p reported=0x%x",
      phase, (unsigned)result, address, result == 0 ? (unsigned)reported(address, NULL) : 0u);
  if (result == 0 && address) {
    try_exec(phase, "flexible-rwx", address, address);
    munmap(address, SMALL);
  }
}

static void test_jit(const char* phase) {
  int fd = -1;
  int result = sceKernelJitCreateSharedMemory("og-probe", SMALL, RWX, &fd);
  say("[probe] %s jit-shared create=0x%x fd=%d", phase, (unsigned)result, fd);
  if (result != 0 || fd < 0)
    return;

  void* both = NULL;
  result = sceKernelJitMapSharedMemory(fd, RWX, &both);
  say("[probe] %s jit-shared-rwx map=0x%x address=%p reported=0x%x", phase, (unsigned)result, both,
      result == 0 ? (unsigned)reported(both, NULL) : 0u);
  if (result == 0 && both)
    try_exec(phase, "jit-shared-rwx", both, both);

  /* The interface's intended shape: one writable view and one executable view. */
  void* writable = NULL;
  result = sceKernelJitMapSharedMemory(fd, RW, &writable);
  say("[probe] %s jit-alias map-rw=0x%x address=%p reported=0x%x", phase, (unsigned)result,
      writable, result == 0 ? (unsigned)reported(writable, NULL) : 0u);
  int alias = -1;
  int alias_result = sceKernelJitCreateAliasOfSharedMemory(fd, RX, &alias);
  say("[probe] %s jit-alias create-alias=0x%x fd=%d", phase, (unsigned)alias_result, alias);
  if (result == 0 && writable && alias_result == 0 && alias >= 0) {
    void* executable = NULL;
    result = sceKernelJitMapSharedMemory(alias, RX, &executable);
    say("[probe] %s jit-alias map-rx=0x%x address=%p reported=0x%x", phase, (unsigned)result,
        executable, result == 0 ? (unsigned)reported(executable, NULL) : 0u);
    if (result == 0 && executable) {
      try_exec(phase, "jit-alias", writable, executable);
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

/* The mapping game/runtime.cpp makes: 128 MiB, PROT_EXEC|READ|WRITE, at the EE hint, fd -1. */
static void test_ee_main_memory(const char* phase) {
  const char* test = "ee-main-rwx";
  void* address = map_anonymous(phase, test, (void*)EE_MAIN_MEM_MAP, EE_MAIN_MEM_SIZE, RWX, -1);
  if (!address) {
    test = "ee-main-rw";
    address = map_anonymous(phase, test, (void*)EE_MAIN_MEM_MAP, EE_MAIN_MEM_SIZE, RW, -1);
  }
  if (!address)
    return;
  say("[probe] %s %s hint=%s", phase, test,
      address == (void*)EE_MAIN_MEM_MAP ? "honored" : "ignored");
  /* One megabyte in, clear of the low region runtime.cpp protects. */
  uint8_t* probe_page = (uint8_t*)address + (1u << 20);
  if ((reported(probe_page, NULL) & RW) == RW) {
    g_fault_signal = 0;
    if (sigsetjmp(g_fault_jump, 1) == 0) {
      g_fault_armed = 1;
      memset(probe_page, 0, 4096);
      g_fault_armed = 0;
    } else {
      say("[probe] %s %s commit=fault signal=%d", phase, test, (int)g_fault_signal);
    }
  }
  try_exec(phase, test, probe_page, probe_page);
  munmap(address, EE_MAIN_MEM_SIZE);
}

static void run_checks(const char* phase) {
  say("[probe] %s uid=%d euid=%d", phase, (int)getuid(), (int)geteuid());
  test_mmap(phase);
  test_flexible(phase);
  test_jit(phase);
  test_ee_main_memory(phase);
  say("[probe] %s checks complete", phase);
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

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  open_log();
  say("[probe] start pid=%d uid=%d pagesize=%d", (int)getpid(), (int)getuid(), getpagesize());

  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = on_fault;
  sigemptyset(&action.sa_mask);
  int handlers = sigaction(SIGSEGV, &action, NULL) | sigaction(SIGBUS, &action, NULL) |
                 sigaction(SIGILL, &action, NULL);
  say("[probe] fault-handlers sigaction=%d errno=%d", handlers, handlers ? errno : 0);

  /* 1. Before elevation. */
  run_checks("pre-elevation");

  /* 2. Request the existing, qualified title-scoped elevation. */
  say("[probe] elevation requesting (title-scoped filesystem capability)");
  int status = og_title_elevate();
  say("[probe] elevation status=%d path=%s uid=%d euid=%d (status 0 == ok)", status,
      og_title_elevate_path(), (int)getuid(), (int)geteuid());

  /* 3. After elevation: exactly the same checks. */
  run_checks("post-elevation");

  signal(SIGSEGV, SIG_DFL);
  signal(SIGBUS, SIG_DFL);
  signal(SIGILL, SIG_DFL);

  /* 4/5: a single line the hardware cycle can wait on, then exit cleanly. */
  say("[probe] elevated-execmem complete elevation-status=%d", status);
  if (g_log) {
    fflush(g_log);
    fclose(g_log);
  }
  return 0;
}
