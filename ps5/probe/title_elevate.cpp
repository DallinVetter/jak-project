// C shim over the boilerplate's title-scoped elevation client, for the executable-memory probe.
//
// elevation::request(filesystem) is the already-documented, already-qualified mechanism
// (../ps5-native-app-boilerplate/docs/SANDBOX_ELEVATION.md): it asks a resident helper, or the
// packaged /app0/lapy.elf sent to the local ELF loader, to raise THIS title's own process
// credentials, and returns ok only after a /data write/read/list proof. The title calls no
// kernel primitive itself. The probe calls this once, between its two executable-memory passes,
// to see whether elevated credentials change what the kernel grants.
//
// Scope: this title (PPSA99005) and the qualified development environment only. Not an
// elevation tool and not wired into the OpenGOAL runtime.
#include "sandbox-elevation/elevation.hpp"

extern "C" {

// elevation::Status as an int (0 == ok), see elevation.hpp.
int og_title_elevate(void) {
  return static_cast<int>(elevation::request(elevation::Capability::filesystem));
}

// Which path the client took: "existing", "resident", "helper", or "none".
const char* og_title_elevate_path(void) {
  return elevation::path();
}
}
