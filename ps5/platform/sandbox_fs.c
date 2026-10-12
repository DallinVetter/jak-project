/* File status inside the title sandbox.
 *
 * ShadowMount Plus mounts /data into a sandboxed title so that it can stat, open and write
 * files there, but lstat is refused with EPERM. OpenGOAL's fs:: is ghc::filesystem, which asks
 * lstat first for every status query (exists, is_directory, create_directories), so each of
 * them throws "Operation not permitted" on a path that is in fact usable.
 *
 * Linked with --wrap=lstat (ps5/build_gk.sh): when lstat is refused, answer with stat. The only
 * difference is that a symbolic link is reported as its target, and a title has no links to
 * tell apart. */
#include <errno.h>
#include <sys/stat.h>

int __real_lstat(const char* path, struct stat* status);

int __wrap_lstat(const char* path, struct stat* status) {
  int result = __real_lstat(path, status);
  if (result != 0 && errno == EPERM) {
    result = stat(path, status);
  }
  return result;
}
