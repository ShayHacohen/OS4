#include <unistd.h>
#include <cstring>

#ifdef DEBUG
#include <iostream>
#endif

struct MallocMetadata {
    size_t size;
    bool is_free;
    MallocMetadata* next;
    MallocMetadata* prev;
    MallocMetadata* buddy;
};

const size_t BASE_ORDER_SIZE = 128;
const int MAX_ORDER = 10;
const unsigned long BLOCK_COUNT = 32;

const int ORDER_COUNT = MAX_ORDER + 1;

class BuddyAllocator {
private:
    int base_order;
    MallocMetadata* blocks[MAX_ORDER + 1];
    bool initialized = false;

    //Auxiliary & convenience member functions & properties:
    size_t order_map[MAX_ORDER + 1];  //Just for minor runtime optimization purposes.
    float kb_to_b(float kb) { return kb / 1024; }
    float b_to_kb(float b) { return b * 1024; }
    MallocMetadata* __aux_getBlockByAddressTraversal(int order, int index) {
        return (MallocMetadata*)((char*)blocks[order] + order_map[order] * (index));
    }
public:
    BuddyAllocator(int base_order=BASE_ORDER_SIZE)
        : base_order(base_order) {
        blocks[0] = nullptr;
        order_map[0] = base_order;
        for (int i = 1; i < ORDER_COUNT; ++i) {
            order_map[i] = 2 * order_map[i-1];
            blocks[i] = nullptr;
        }
    }

    void initialize_blocks() {
        if (initialized) { return; }
        initialized = true;

        #ifdef DEBUG
            std::cout << "Initializing buddy allocator." << std::endl;
        #endif

        blocks[ORDER_COUNT - 1] = (MallocMetadata*)sbrk(BLOCK_COUNT * order_map[MAX_ORDER]);

        *__aux_getBlockByAddressTraversal(MAX_ORDER, 0) = { order_map[ORDER_COUNT - 1], true, nullptr, nullptr, nullptr };
        blocks[ORDER_COUNT - 1]->next = __aux_getBlockByAddressTraversal(MAX_ORDER, 1);
        *__aux_getBlockByAddressTraversal(MAX_ORDER, BLOCK_COUNT - 1) = { order_map[ORDER_COUNT - 1], true, nullptr, nullptr, nullptr };
        __aux_getBlockByAddressTraversal(MAX_ORDER, ORDER_COUNT - 1)->next = __aux_getBlockByAddressTraversal(MAX_ORDER, ORDER_COUNT - 2);

        for (int i = 1; i < BLOCK_COUNT - 1; ++i) {
            auto curr = __aux_getBlockByAddressTraversal(MAX_ORDER, i);
            *curr = { order_map[ORDER_COUNT - 1], true, nullptr, nullptr, nullptr };
            curr->prev = __aux_getBlockByAddressTraversal(MAX_ORDER, i - 1);
            curr->next = __aux_getBlockByAddressTraversal(MAX_ORDER, i + 1);
        }
    }

    MallocMetadata* getMinimalMatchingBlock(size_t size, bool split=true) {
        for (int i = 0; i < ORDER_COUNT; ++i) {
            if (order_map[i] < size + sizeof(MallocMetadata)) {
                continue;
            }

            auto curr = blocks[i];
            while (curr != nullptr) {
                if (curr->is_free) {
                    return curr;
                }
                curr = curr->next;
            }
        }
        return nullptr;
    }

    //TESTING STUFF:
    void TEST_print_orders() {
    #ifdef DEBUG
        initialize_blocks();
        for (int i = 0; i < ORDER_COUNT; ++i) {
            std::cout << "Order #" << i << ": " << order_map[i] << std::endl;
        }
    #endif
    }
    void TEST_print_blocks() {
    #ifdef DEBUG
        initialize_blocks();
        for (int i = 0; i < ORDER_COUNT; ++i) {
            std::cout << "@ @ @\nIterating over blocks of order " << i << " (size: " <<
                order_map[i] << " bytes)." << std::endl;
            auto list = blocks[i];
            int j = 0;
            while (list != nullptr) {
                std::cout << "Block #" << j++ << ": addr=" << list << ", size=" << list->size
                    << ", " << (list->is_free ? "" : "not ") << "free, buddy is " << list->buddy
                    << std::endl;
                list = list->next;
            }
        }
    #endif
    }

    void TEST_minimal_matching_no_split() {
    #ifdef DEBUG
        initialize_blocks();
        size_t test_set[] = {5, 17, 90, 44, 33, 128, 128 * 1023, 128 * 1000, 128 * 1024 - 41, 128 * 1024 - 40, 128 * 1024 - 39, 128 * 1024};
        for (auto i : test_set) {
            std::cout << "Minimal matching for " << i << " without splitting: "
                << getMinimalMatchingBlock(i, false) << std::endl;
        }
        std::cout << "(MallocMetadata size is " << sizeof(MallocMetadata) << ".)" << std::endl;
    #endif
    }
};

MallocMetadata *allocations = nullptr;
MallocMetadata *last = nullptr;

int free_blocks = 0;
int total_allocated_blocks = 0;
size_t allocated_space = 0;
size_t free_space = 0;

auto allocator = BuddyAllocator();

void TEST_print_orders() {
    allocator.TEST_print_orders();
}

void TEST_print_blocks() {
    allocator.TEST_print_blocks();
}

void TEST_several_stuff() {
    allocator.TEST_minimal_matching_no_split();
}

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
