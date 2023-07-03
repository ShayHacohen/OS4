#include <unistd.h>
#include <cstring>

#ifdef DEBUG
#include <iostream>
#endif

const size_t BASE_ORDER_SIZE = 128;
const int MAX_ORDER = 10;
const unsigned long BLOCK_COUNT = 32;

const int ORDER_COUNT = MAX_ORDER + 1;

struct MallocMetadata {
    size_t size;
    bool is_free;
    MallocMetadata* next;
    MallocMetadata* prev;
};

class BuddyAllocator {
private:
    MallocMetadata* base_heap_addr = nullptr;
    int base_order;
    MallocMetadata* free_blocks[MAX_ORDER + 1];
    MallocMetadata* used_blocks = nullptr;
    bool initialized = false;
    int free_block_count = 0;
    int total_allocated_blocks = 0;
    size_t allocated_space = 0;
    size_t free_space = 0;

    //Auxiliary & convenience member functions & properties:
    size_t order_map[ORDER_COUNT];  //Just for minor runtime optimization purposes.
    int order_from_size(size_t size) {
        for (int i = 0; i < ORDER_COUNT; ++i) {
            if (order_map[i] == size) {
                return i;
            }
        }
        return -1;
    }
    static float kb_to_b(float kb) { return kb / 1024; }
    static float b_to_kb(float b) { return b * 1024; }

    MallocMetadata* __aux_getBlockByAddressTraversal(int order, int index) {
        return (MallocMetadata*)((char*)free_blocks[order] + order_map[order] * (index));
    }
    void aux_removeFromBlocksList(MallocMetadata* block, MallocMetadata** head=nullptr) {
        if (!head) {
            if (block->is_free) {
                #ifdef DEBUG
                std::cout << "aux_removeFromBlocksList only supports implicit list head if block is allocated." << std::endl;
                #endif
                return;
            }
            head = &used_blocks;
        }
        #ifdef DEBUG
        if (*head == nullptr) {
            std::cout << "Tried to remove block from an empty list!" << std::endl;
            return;
        }
        #endif
        if (block->prev) {
            block->prev->next = block->next;
        }
        if (block->next) {
            block->next->prev = block->prev;
        }
        if (*head == block) {
            *head = block->next;
        }
        block->next = nullptr;
        block->prev = nullptr;
    }

    //TODO: handle realloc
    size_t aux_getMaxMergeableSize(MallocMetadata* block) {
        auto curr = block;
        auto curr_size = block->size;
        while (order_from_size(curr->size) < MAX_ORDER) {
            auto buddy = aux_getBuddy(block, curr_size);
            if (!buddy) {
                break;
            }
            curr = block < buddy ? block : buddy;
            curr_size = block->size + buddy->size;
        }
        return curr_size;
    }

    void aux_addToBlocksList(MallocMetadata** head_ptr, MallocMetadata* block) {
        if (*head_ptr == nullptr) {
            *head_ptr = block;
            return;
        }
        else if ((*head_ptr) > block) {
            block->next = *head_ptr;
            block->next->prev = block;
            *head_ptr = block;
            return;
        }

        auto head = *head_ptr;
        MallocMetadata *next = head->next;

        while(head) {
            if (next == nullptr) {
                next = block;
                return;
            }
            else if (head->next < block) {
                block->next = next;
                next->prev = block;
                head->next = block;
                return;
            }
            head = head->next;
        }
        #ifdef DEBUG
        std::cout << "ERROR: aux_addToBlocksList failed." << std::endl;
        #endif
    }

    void aux_addToFreeBlocks(MallocMetadata* block) {
        aux_addToBlocksList(&free_blocks[order_from_size(block->size)], block);
        block->is_free = true;
    }

    void aux_removeFromFreeBlocks(MallocMetadata* block) {
        int order = order_from_size(block->size);
        if (order < 0) {
            #ifdef DEBUG
            std::cout << "Attempted to remove block with illegal size: " << block->size << "std::endl";
            #endif
            return;
        }
        aux_removeFromBlocksList(block, &free_blocks[order]);
    }

    void aux_mergeStep(MallocMetadata** block_ptr) {
        auto block = *block_ptr;
        auto buddy = aux_getBuddy(block);
        if (!(buddy != nullptr && buddy->is_free)) {
            #ifdef DEBUG
            std::cout << "Called aux_mergeStep with non-mergeable blocks.";
            #endif
            return;
        }
        auto left_buddy = block < buddy ? block : buddy;
        auto right_buddy = block < buddy ? buddy : block; //Could do sum - min, but not sure if sum might overflow...
        block = left_buddy;
        buddy = right_buddy;
        block->size += buddy->size;
        auto &free_list = free_blocks[order_from_size(buddy->size)]; //I hope references don't take up heap storage...
        aux_removeFromBlocksList(block, &free_list);
        aux_removeFromBlocksList(buddy, &free_list);
        aux_addToFreeBlocks(block);
        *block_ptr = block;

        //Statistics changes due to merging:
        --free_block_count;
        free_space += sizeof(MallocMetadata);
        allocated_space += sizeof(MallocMetadata);
        --total_allocated_blocks;
    }

    /*
     * This function could be substituted by keeping an integer and using it as a binary value,
     * where each bit signifies whether the block is the left buddy or the right one
     * (i.e whether its address was lower or higher, respectively). This only requires an extra
     * integer and so wouldn't violate the metadata space requirements. This solution makes the metadata
     * even smaller, though, so I'm going for that (although it might be a little slower).
     */
    MallocMetadata* aux_getBuddy(MallocMetadata* block, size_t overwrite_size=0) {
        size_t block_size = overwrite_size ? overwrite_size : block->size;
        if (!initialized) return nullptr; //Shouldn't happen, but eh.
        if (block_size == order_map[MAX_ORDER]) return nullptr; //No buddies for max-order blocks. (It's lonely at the top or something)

        //Determine if left buddy or right buddy (i.e if buddy should have lower or higher address):
        if ((((long)block - (long)base_heap_addr) % block_size) != 0) { //Sanity check but I'll leave it here
            #ifdef DEBUG
                std::cout << "getBuddy sanity check failed: block addr is " << block
                    << ", base heap addr is " << base_heap_addr
                    << ", modulo results in " << (((long)block - (long)base_heap_addr) % block_size)
                    << std::endl;
            #endif
            return nullptr;
        }

        bool left = (((long)block - (long)base_heap_addr) / block->size) % 2 == 0;
        auto buddy = (MallocMetadata*)(left ? ((char*)block + block->size) : ((char*)block - block->size));
        if (!buddy->is_free || buddy->size != block->size)
        {
            return nullptr; //Buddy is either allocated or split into a smaller chunk.
        }
        return buddy;

    }
public:
    BuddyAllocator(int base_order=BASE_ORDER_SIZE)
        : base_order(base_order) {
        free_blocks[0] = nullptr;
        order_map[0] = base_order;
        for (int i = 1; i < ORDER_COUNT; ++i) {
            order_map[i] = 2 * order_map[i-1];
            free_blocks[i] = nullptr;
        }
    }

    void initialize_blocks() {
        if (initialized) { return; }
        initialized = true;

        #ifdef DEBUG
        std::cout << "Initializing buddy allocator." << std::endl;
        #endif

        free_blocks[ORDER_COUNT - 1] = base_heap_addr = (MallocMetadata*)sbrk(BLOCK_COUNT * order_map[MAX_ORDER]);

        *__aux_getBlockByAddressTraversal(MAX_ORDER, 0) = { order_map[ORDER_COUNT - 1], true, nullptr, nullptr };
        free_blocks[ORDER_COUNT - 1]->next = __aux_getBlockByAddressTraversal(MAX_ORDER, 1);
        *__aux_getBlockByAddressTraversal(MAX_ORDER, BLOCK_COUNT - 1) = { order_map[ORDER_COUNT - 1], true, nullptr, nullptr };
        __aux_getBlockByAddressTraversal(MAX_ORDER, ORDER_COUNT - 1)->next = __aux_getBlockByAddressTraversal(MAX_ORDER, ORDER_COUNT - 2);

        for (int i = 1; i < BLOCK_COUNT - 1; ++i) {
            auto curr = __aux_getBlockByAddressTraversal(MAX_ORDER, i);
            *curr = { order_map[ORDER_COUNT - 1], true, nullptr, nullptr };

            curr->prev = __aux_getBlockByAddressTraversal(MAX_ORDER, i - 1);
            curr->next = __aux_getBlockByAddressTraversal(MAX_ORDER, i + 1);
        }

        free_block_count = BLOCK_COUNT;
        free_space = BLOCK_COUNT * (order_map[MAX_ORDER] - sizeof(MallocMetadata));
        allocated_space = free_space;
        total_allocated_blocks = BLOCK_COUNT;
    }

    MallocMetadata* getMinimalMatchingFreeBlock(size_t size) {
        for (int i = 0; i < ORDER_COUNT; ++i) {
            if (order_map[i] < size + sizeof(MallocMetadata)) {
                continue;
            }

            auto curr = free_blocks[i];
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
    void TEST_print_orders();
    void TEST_print_blocks();
    void TEST_minimal_matching_no_split();

    MallocMetadata *allocateBlock(size_t size);
    MallocMetadata* attemptInPlaceRealloc(MallocMetadata* block, size_t size);
    void setBlockFree(MallocMetadata *block, bool free_value, size_t requested_size=0);
    MallocMetadata* performMerge(MallocMetadata *block, size_t requested_size=0);

    size_t _num_free_blocks() const;
    size_t _num_free_bytes() const;
    size_t _num_allocated_blocks() const;
    size_t _num_allocated_bytes() const;
    size_t _num_meta_data_bytes() const;
    size_t _size_meta_data() const;

    int aux_full_fetch_of_free_blocks(int *bytes=nullptr, int *bytesWithoutMetadata=nullptr);
    int aux_full_fetch_of_used_blocks(int *bytes=nullptr, int *bytesWithoutMetadata=nullptr);
    int aux_full_fetch_of_allocated_blocks();
    int aux_full_fetch_of_metadata_bytes ();
    int aux_full_fetch_of_free_bytes();
    int aux_full_fetch_of_free_bytes_with_metadata();
    int aux_full_fetch_of_used_bytes();
    int aux_full_fetch_of_used_bytes_with_metadata();
    int aux_full_fetch_of_allocated_bytes();
    int aux_full_fetch_of_allocated_bytes_with_metadata();
};

MallocMetadata* BuddyAllocator::performMerge(MallocMetadata *block, size_t requested_size) {
    free_space += block->size - sizeof(MallocMetadata);
    ++free_block_count;

    //Remove from used blocks list:
    aux_removeFromBlocksList(block);
    aux_addToFreeBlocks(block);

    //Merge:
    MallocMetadata* buddy;
    while ((buddy = aux_getBuddy(block)) != nullptr
            && (requested_size <= 0 || requested_size > block->size - sizeof(MallocMetadata))) {
        if (!buddy->is_free) {
            break;
        }
        aux_mergeStep(&block);
    }

    return block;
}

void BuddyAllocator::setBlockFree(MallocMetadata *block, bool free_value, size_t requested_size) {
    if (free_value == block->is_free) {
#ifdef DEBUG
        std::cout << "WARNING: Attempting to free a block that was already freed!" << std::endl;
#endif
        return;
    }
    if (free_value) {
        block = performMerge(block);
    }
    else {
        free_space -= block->size - sizeof(MallocMetadata);
        --free_block_count;
        aux_removeFromFreeBlocks(block);
        aux_addToBlocksList(&used_blocks, block);

        //Split:
        while (!(
                order_from_size(block->size) <= 0 //Got to minimal order, or
                || requested_size > ((block->size/2) - sizeof(MallocMetadata)) //any smaller is too small
            )) {
            auto buddy = (MallocMetadata*)((char*)block + block->size/2);
            block->size /= 2;
            buddy->size = block->size;
            buddy->is_free = true;
            buddy->next = nullptr;
            buddy->prev = nullptr;
            ++free_block_count;
            ++total_allocated_blocks;
            free_space += buddy->size - sizeof(MallocMetadata);
            allocated_space -= sizeof(MallocMetadata);
            aux_addToFreeBlocks(buddy);
        }
    }
    block->is_free = free_value;
}

MallocMetadata *BuddyAllocator::allocateBlock(size_t size) {
    initialize_blocks();

    if (size == 0 || size > 100000000) return nullptr;

    auto block = getMinimalMatchingFreeBlock(size);
    setBlockFree(block, false, size);
    return block;
}

/*
* NOTE: not actually in-place, but achieved by merging with buddies iteratively until
* a matching size is acheived
*/
MallocMetadata* BuddyAllocator::attemptInPlaceRealloc(MallocMetadata* block, size_t size) {
    if (size > aux_getMaxMergeableSize(block) - sizeof(MallocMetadata)) {
        return nullptr;
    }
    return performMerge(block, size);
}

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

void* smalloc(size_t size) {
    auto block_ptr = allocator.allocateBlock(size);
    if (block_ptr != nullptr) {
        block_ptr += 1;
    }
    return block_ptr;
}

void* scalloc(size_t num, size_t size) {
    auto addr = allocator.allocateBlock(num * size);
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

    allocator.setBlockFree(pointer, true);
}

void *srealloc(void* oldp, size_t size) {
    //TODO: consider what to do if oldp is free
    if (oldp == nullptr) {
        return smalloc(size);
    }
    auto old_block = (MallocMetadata*)oldp;
    --old_block;

    if (size <= old_block->size - sizeof(MallocMetadata)) {
        return oldp;
    }

    auto newp = allocator.attemptInPlaceRealloc(old_block, size);
    //TODO: check if malloc_2 handles this right (do we want to free old_block here for sure?)

    if (newp) {
        std::memmove(newp, oldp, size);
        return newp;
    }

    newp = (MallocMetadata*)smalloc(size);

    if (!newp) {
        return nullptr;
    }

    std::memmove(newp, oldp, size);
    allocator.setBlockFree(old_block, true);

    return newp;
}



//STATISTICS FUNCTIONS:
size_t BuddyAllocator::_num_free_blocks() const {
    return free_block_count;
}

size_t BuddyAllocator::_num_free_bytes() const {
    return free_space;
}

size_t BuddyAllocator::_num_allocated_blocks() const {
    return total_allocated_blocks;
}

size_t BuddyAllocator::_num_allocated_bytes() const {
    return allocated_space;
}

size_t BuddyAllocator::_num_meta_data_bytes() const {
    return total_allocated_blocks * sizeof(MallocMetadata);
}

size_t BuddyAllocator::_size_meta_data() const {
    return sizeof(MallocMetadata);
}


//TESTING STUFF:
void BuddyAllocator::TEST_minimal_matching_no_split() {
#ifdef DEBUG
    initialize_blocks();
    size_t test_set[] = {5, 17, 90, 44, 33, 128, 128 * 1023, 128 * 1000, 128 * 1024 - 41, 128 * 1024 - 40, 128 * 1024 - 39, 128 * 1024};
    for (auto i : test_set) {
        std::cout << "Minimal matching for " << i << " without splitting: "
                  << getMinimalMatchingFreeBlock(i) << std::endl;
    }
    std::cout << "(MallocMetadata size is " << sizeof(MallocMetadata) << ".)" << std::endl;
#endif
}

void BuddyAllocator::TEST_print_blocks() {
#ifdef DEBUG
    for (int i = 0; i < ORDER_COUNT; ++i) {
        std::cout << "@ @ @\nIterating over free_blocks of order " << i << " (size: " <<
                  order_map[i] << " bytes)." << std::endl;
        auto list = free_blocks[i];
        int j = 0;
        while (list != nullptr) {
            std::cout << "Block #" << j++ << ": addr=" << list << ", size=" << list->size
                      << ", " << (list->is_free ? "" : "not ") << "free.\nBuddy is "
                      << aux_getBuddy(list) << std::endl;
            list = list->next;
        }
    }
#endif
}

void BuddyAllocator::TEST_print_orders() {
#ifdef DEBUG
    initialize_blocks();
    for (int i = 0; i < ORDER_COUNT; ++i) {
        std::cout << "Order #" << i << ": " << order_map[i] << std::endl;
    }
#endif
}

//STATISTICS FUNCTIONS:
size_t _num_free_blocks() {
    return allocator._num_free_blocks();
}

size_t _num_free_bytes() {
    return allocator._num_free_bytes();
}

size_t _num_allocated_blocks() {
    return allocator._num_allocated_blocks();
}

size_t _num_allocated_bytes() {
    return allocator._num_allocated_bytes();
}

size_t _num_meta_data_bytes() {
    return allocator._num_meta_data_bytes();
}

size_t _size_meta_data() {
    return sizeof(MallocMetadata);
}

int BuddyAllocator::aux_full_fetch_of_free_blocks(int *bytes, int *bytesWithoutMetadata) {
    int total_bytes = 0, total_bytes_without_metadata = 0;
    int cnt = 0;
    for (auto list : free_blocks) {
        while (list) {
            ++cnt;
            total_bytes += list->size;
            total_bytes_without_metadata += list->size - sizeof(MallocMetadata);
            list = list->next;
        }
    }

    if (bytes) *bytes = total_bytes;
    if (bytesWithoutMetadata) *bytesWithoutMetadata = total_bytes_without_metadata;

    return cnt;
}

int BuddyAllocator::aux_full_fetch_of_used_blocks(int *bytes, int *bytesWithoutMetadata) {
    int total_bytes = 0, total_bytes_without_metadata = 0;
    int cnt = 0;
    auto curr = used_blocks;
    while (curr) {
        total_bytes += curr->size;
        total_bytes_without_metadata += curr->size - sizeof(MallocMetadata);
        ++cnt;
        curr = curr->next;
    }

    if (bytes) *bytes = total_bytes;
    if (bytesWithoutMetadata) *bytesWithoutMetadata = total_bytes_without_metadata;

    return cnt;
}

int BuddyAllocator::aux_full_fetch_of_allocated_blocks() {
    return aux_full_fetch_of_free_blocks() + aux_full_fetch_of_used_blocks();
}

int BuddyAllocator::aux_full_fetch_of_metadata_bytes () {
    return aux_full_fetch_of_allocated_blocks() * (int)sizeof(MallocMetadata);
}

int BuddyAllocator::aux_full_fetch_of_free_bytes_with_metadata() {
    int bytes;
    aux_full_fetch_of_free_blocks(&bytes);
    return bytes;
}

int BuddyAllocator::aux_full_fetch_of_free_bytes() {
    int bytesWithoutMetadata;
    aux_full_fetch_of_free_blocks(nullptr, &bytesWithoutMetadata);
    return bytesWithoutMetadata;
}

int BuddyAllocator::aux_full_fetch_of_used_bytes_with_metadata() {
    int bytes, bytesWithoutMetadata;
    aux_full_fetch_of_used_blocks(&bytes, &bytesWithoutMetadata);
    return bytes;
}

int BuddyAllocator::aux_full_fetch_of_used_bytes() {
    int bytesWithoutMetadata;
    aux_full_fetch_of_used_blocks(nullptr, &bytesWithoutMetadata);
    return bytesWithoutMetadata;
}

int BuddyAllocator::aux_full_fetch_of_allocated_bytes_with_metadata() {
    return aux_full_fetch_of_free_bytes_with_metadata() + aux_full_fetch_of_used_bytes_with_metadata();
}

int BuddyAllocator::aux_full_fetch_of_allocated_bytes() {
    return aux_full_fetch_of_free_bytes() + aux_full_fetch_of_used_bytes();
}

int FULL_free_blocks_count() {
    return allocator.aux_full_fetch_of_free_blocks();
}
int FULL_free_blocks_bytes() {
    return allocator.aux_full_fetch_of_free_bytes();
}
int FULL_used_blocks_count() {
    return allocator.aux_full_fetch_of_used_blocks();
}
int FULL_used_blocks_bytes() {
    return allocator.aux_full_fetch_of_used_bytes();
}
int FULL_allocated_blocks_count() {
    return allocator.aux_full_fetch_of_allocated_blocks();
}
int FULL_allocated_blocks_bytes() {
    return allocator.aux_full_fetch_of_allocated_bytes();
}
int FULL_metadata_bytes () {
    return allocator.aux_full_fetch_of_metadata_bytes();
}
int FULL_free_blocks_bytes_with_metadata() {
    return allocator.aux_full_fetch_of_free_bytes_with_metadata();
}
int FULL_used_blocks_bytes_with_metadata() {
    return allocator.aux_full_fetch_of_used_bytes_with_metadata();
}
int FULL_allocated_blocks_bytes_with_metadata() {
    return allocator.aux_full_fetch_of_allocated_bytes_with_metadata();
}