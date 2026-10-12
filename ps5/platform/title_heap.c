/* Heap size for the title.
 *
 * ps5-opengl's allocator (native-app/app_heap.c, linked into every title) serves malloc from
 * one region whose size a title sets with this symbol. Its default is 128 MiB of flexible
 * memory, of which a title has about 416 MiB in total; EE main memory alone takes 128 MiB of
 * that. Above 128 MiB the allocator uses direct memory instead, so the runtime's own
 * allocations (texture pool, level loader, renderer buffers) do not compete with it.
 * 1 GiB is the size ps5-opengl's CTS runner uses. */
#include <stddef.h>

const size_t ps5_opengl_heap_size = (size_t)1 << 30;
