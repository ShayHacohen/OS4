#include <unistd.h>

void* smalloc (size_t size) {
    if (size == 0 || size > 100000000) return nullptr;
    void *addr;
    if ((addr = sbrk(size)) == (void*)-1) {
        return nullptr;
    }
    return addr;
}