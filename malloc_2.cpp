#include <unistd.h>
#include <cstring>

struct MallocMetadata {
    size_t size;
    bool is_free;
    MallocMetadata* next;
    MallocMetadata* prev;
};

MallocMetadata *allocations = nullptr;
MallocMetadata *last = nullptr;

int free_blocks = 0;
int total_allocated_blocks = 0;
size_t allocated_space = 0;
size_t free_space = 0;

void __setBlockFree(MallocMetadata *block, bool free_value) {
    if (free_value == block->is_free) return;
    if (free_value) {
        free_space += block->size;
        ++free_blocks;
    }
    else {
        free_space -= block->size;
        --free_blocks;
    }
    block->is_free = free_value;
}

MallocMetadata *__findFreeBlock(size_t size) {
    auto curr = allocations;
    while (curr != nullptr) {
        if (curr->is_free && curr->size >= size) {
            return curr;
        }
        curr = curr->next;
    }
    return nullptr;
}

MallocMetadata *__addBlock(size_t size) {
    MallocMetadata *addr;
    if ((addr = (MallocMetadata *)sbrk(size + sizeof(MallocMetadata))) == (void*)-1) {
        return nullptr;
    }

    ++total_allocated_blocks;
    allocated_space += size;

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

MallocMetadata* __findOrAllocateBlock(size_t size) {
    if (size == 0 || size > 100000000) return nullptr;

    auto block = __findFreeBlock(size);
    if (block) {
        __setBlockFree(block, false);
        return block;
    }

    return __addBlock(size);
}

void* smalloc(size_t size) {
    auto block_ptr = __findOrAllocateBlock(size);
    if (block_ptr != nullptr) {
        block_ptr += 1;
    }
    return block_ptr;
}

void* scalloc(size_t num, size_t size) {
    auto addr = __findOrAllocateBlock(num * size);
    if (addr == nullptr) {
        return nullptr;
    }

    std::memset(addr + 1, 0, num * size);

    return addr + 1;
}

void sfree(void* p) {
    if (p == nullptr) return;

    auto pointer = (MallocMetadata*)p;
    --pointer; //To make it point to the metadata

    if (pointer->is_free) {
        return;
    }

    __setBlockFree(pointer, true);
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

    std::memmove(newp, oldp, size);

    __setBlockFree(old_block, true);
    return newp;
}



//STATISTICS FUNCTIONS:
size_t _num_free_blocks() {
    return free_blocks;
}

size_t _num_free_bytes() {
    return free_space;
}

size_t _num_allocated_blocks() {
    return total_allocated_blocks;
}

size_t _num_allocated_bytes() {
    return allocated_space;
}

size_t _num_meta_data_bytes() {
    return total_allocated_blocks * sizeof(MallocMetadata);
}

size_t _size_meta_data() {
    return sizeof(MallocMetadata);
}
