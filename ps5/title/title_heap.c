/* Heap size for the title.
 *
 * ps5-opengl's allocator (native-app/app_heap.c, linked into every title) serves malloc from
 * one region whose size a title sets with this symbol. Its default is 128 MiB of flexible
 * memory, of which a title has about 416 MiB in total. With real game data gk ran out while
 * uploading the common level's textures: the receipt ends in
 * "[ScePthread/System] Internal Memory is running out." (2026-10-12,
 * results/PPSA99005-20261012T223119Z).
 *
 * Above 128 MiB the allocator takes direct memory instead, leaving flexible memory to EE main
 * memory and the system's own needs. 1 GiB is the size ps5-opengl's CTS runner uses.
 *
 * This must be linked as an object (ps5/title), not from an archive: see ps5/README.md. */
#include <stddef.h>

const size_t ps5_opengl_heap_size = (size_t)1 << 30;
