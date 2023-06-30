#include <unistd.h>

struct MallocMetadata {
    size_t size;
    bool is_free;
    MallocMetadata* next;
    MallocMetadata* prev;
};

MallocMetadata *allocations = nullptr;
MallocMetadata *last = nullptr;

int free_blocks = 0;
int allocated_blocks = 0;

MallocMetadata *_findFreeBlock(size_t size) {
    auto curr = allocations;
    while (curr != nullptr) {
        if (curr->is_free && curr->size >= size) {
            return curr;
        }
        curr = curr->next;
    }
    return nullptr;
}

MallocMetadata *_addBlock(size_t size) {
    MallocMetadata *addr;
    if ((addr = (MallocMetadata *)sbrk(size + sizeof(MallocMetadata))) == (void*)-1) {
        return nullptr;
    }

    MallocMetadata metadata {
        size, false, nullptr, nullptr
    };
    *addr = metadata;

    if (last == nullptr) {
        allocations = last = addr;
    }
    else {
        last->next = addr;
        addr->prev = last;
        last = addr;
    }
    return addr;
}

MallocMetadata* _allocateBlock(size_t size) {
    if (size == 0 || size > 100000000) return nullptr;

    ++allocated_blocks;

    auto block = _findFreeBlock(size);
    if (block) {
        --free_blocks;
        block->is_free = false;
        return block;
    }

    return _addBlock(size);
}

void* smalloc(size_t size) {
    auto block_ptr = _allocateBlock(size);
    if (block_ptr != nullptr) {
        block_ptr += 1;
    }
    return block_ptr;
}

void* scalloc(size_t num, size_t size) {
    auto addr = _allocateBlock(num * size);
    if (addr == nullptr) {
        return nullptr;
    }

    char* block_as_bytes = (char*)(addr + 1);

    for (int i = 0; i < num * size; ++i) {
        block_as_bytes[i] = 0;
    }

    return block_as_bytes;
}

void sfree(void* p) {
    if (p == nullptr) return;

    auto pointer = (MallocMetadata*)p;
    --pointer; //To make it point to the metadata

    if (pointer->is_free) {
        return;
    }

    pointer->is_free = true;
    ++free_blocks;
    --allocated_blocks;
}

void *srealloc(void* oldp, size_t size) {
    //TODO: consider what to do if oldp is free
    if (oldp == nullptr) {
        return smalloc(size);
    }
    auto old_block = (MallocMetadata*)oldp;
    --old_block;

    if (size <= old_block->size) {
        return oldp;
    }

    auto newp = (char*)smalloc(size);
    if (newp == nullptr) {
        return nullptr;
    }

    for (int i = 0; i < size; ++i) {
        newp[i] = ((char*)oldp)[i];
    }

    old_block->is_free = true;
    return newp;
}