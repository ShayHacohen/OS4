#include <unistd.h>
#include <cstring>
#include <sys/mman.h>
#include <cstdlib>
#include <ctime>

#ifdef DEBUG
#include <iostream>
#endif

const size_t BASE_ORDER_SIZE = 128;
const int MAX_ORDER = 10;
const unsigned long BLOCK_COUNT = 32;
const unsigned long VM_HUGEPAGE_LENGTH = 2048 * 1024; //2048 kB, as per /proc/meminfo on the VM.
const unsigned long SMALLOC_HUGEPAGE_THRESHOLD = 1024 * 1024 * 4; //4 MB, as per the instructions
const unsigned long SCALLOC_HUGEPAGE_THRESHOLD = 1024 * 1024 * 2; //2 MB, as per the instructions

const int ORDER_COUNT = MAX_ORDER + 1;

struct MallocMetadata {
private:
    unsigned int cookie;
    size_t size;
    bool is_free;
    MallocMetadata* next;
    MallocMetadata* prev;
    bool hugepage;
    void validate_cookie(unsigned int true_cookie) const {
        if (cookie != true_cookie) {
            exit(0xdeadbeef);
        }
    }
public:
    MallocMetadata(size_t size, bool is_free, MallocMetadata* next, MallocMetadata* prev, int cookie, size_t singleBlockSize=0)
            : cookie(cookie), size(size), is_free(is_free), next(next), prev(prev), hugepage(isHugepageSized(size, singleBlockSize)) {}

    size_t getSize(unsigned int true_cookie) const {
        validate_cookie(true_cookie);
        return size;
    }

    void addToSize(unsigned int true_cookie, long by) {
        validate_cookie(true_cookie);
        size += by;
    }

    MallocMetadata* split(unsigned int true_cookie) {
        validate_cookie(true_cookie);
        auto buddy = (MallocMetadata*)((char*)this + size/2);
        size /= 2;
        *buddy = MallocMetadata(size, true, nullptr, nullptr, cookie);
        return buddy;
    }

    bool getIsFree(unsigned int true_cookie) const {
        validate_cookie(true_cookie);
        return is_free;
    }

    void setIsFree(unsigned int true_cookie, int new_is_free) {
        validate_cookie(true_cookie);
        is_free = new_is_free;
    }

    MallocMetadata* getNext(unsigned int true_cookie) const {
        validate_cookie(true_cookie);
        return next;
    }

    void setNext(unsigned int true_cookie, MallocMetadata* new_next) {
        validate_cookie(true_cookie);
        next = new_next;
    }

    MallocMetadata* getPrev(unsigned int true_cookie) const {
        validate_cookie(true_cookie);
        return prev;
    }

    void setPrev(unsigned int true_cookie, MallocMetadata* new_prev) {
        validate_cookie(true_cookie);
        prev = new_prev;
    }

    size_t getHugepageAlignedSize(unsigned int true_cookie) {
        validate_cookie(true_cookie);
        if (!hugepage) return size;
        auto hugepage_count = ((int)size / VM_HUGEPAGE_LENGTH) + ((size % VM_HUGEPAGE_LENGTH) != 0);
#ifdef DEBUG
        std::cout << "HUGEPAGE ALIGNED: " << hugepage_count * VM_HUGEPAGE_LENGTH << std::endl;
#endif
        return hugepage_count * VM_HUGEPAGE_LENGTH;
    }

    static bool isHugepageSized(size_t size, size_t singleBlockSize=0) {
        bool hugepage;
        if (singleBlockSize > 0) {
            hugepage = singleBlockSize > SCALLOC_HUGEPAGE_THRESHOLD; //Says *larger*.
        }
        else {
            hugepage = size - sizeof(MallocMetadata) >= SMALLOC_HUGEPAGE_THRESHOLD; //Says *equal-to or larger*.
        }
        return hugepage;
    }
};

class BuddyAllocator {
private:
    MallocMetadata* base_heap_addr = nullptr;
    int base_order;
    MallocMetadata* free_blocks[MAX_ORDER + 1];
    MallocMetadata* used_blocks = nullptr;
    MallocMetadata* mmapped_blocks = nullptr;
    bool initialized = false;
    int free_block_count = 0;
    int total_allocated_blocks = 0;
    size_t allocated_space = 0;
    size_t free_space = 0;
    int cookie = 0;

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

    MallocMetadata* aux_getBlockByAddressTraversal(int order, int index) {
        return (MallocMetadata*)((char*)free_blocks[order] + order_map[order] * (index));
    }
    void aux_removeFromBlocksList(MallocMetadata* block, MallocMetadata** head=nullptr) {
        if (!head) {
            if (block->getIsFree(cookie)) {
#ifdef DEBUG
                std::cout << "aux_removeFromBlocksList only supports implicit list head if block is allocated." << std::endl;
#endif
                return;
            }
            head = isMemoryMapped(block) ? &mmapped_blocks : &used_blocks;
        }
#ifdef DEBUG
        if (*head == nullptr) {
            std::cout << "Tried to remove block from an empty list!" << std::endl;
            return;
        }
#endif
        if (block->getPrev(cookie)) {
            block->getPrev(cookie)->setNext(cookie, block->getNext(cookie));
        }
        if (block->getNext(cookie)) {
            block->getNext(cookie)->setPrev(cookie, block->getPrev(cookie));
        }
        if (*head == block) {
            *head = block->getNext(cookie);
        }
        block->setNext(cookie, nullptr);
        block->setPrev(cookie, nullptr);
    }

    size_t aux_getMaxMergeableSize(MallocMetadata* block) {
        auto curr = block;
        auto curr_size = block->getSize(cookie);
        while (order_from_size(curr->getSize(cookie)) < MAX_ORDER) {
            auto buddy = aux_getBuddy(curr, curr_size);
            if (!buddy) {
                break;
            }
            curr = curr < buddy ? curr : buddy;
            curr_size = curr_size + buddy->getSize(cookie);
        }
        return curr_size;
    }

    void aux_addToBlocksList(MallocMetadata** head_ptr, MallocMetadata* block) {
        if (*head_ptr == nullptr) {
            *head_ptr = block;
            return;
        }
        else if ((*head_ptr) > block) {
            block->setNext(cookie, *head_ptr);
            block->getNext(cookie)->setPrev(cookie, block);
            *head_ptr = block;
            return;
        }

        auto head = *head_ptr;
        MallocMetadata *next = head->getNext(cookie);

        while(head) {
            if (next == nullptr) {
                next = block;
                return;
            }
            else if (head->getNext(cookie) < block) {
                block->setNext(cookie, next);
                next->setPrev(cookie, block);
                head->setNext(cookie, block);
                return;
            }
            head = head->getNext(cookie);
        }
#ifdef DEBUG
        std::cout << "ERROR: aux_addToBlocksList failed." << std::endl;
#endif
    }

    void aux_addToFreeBlocks(MallocMetadata* block) {
        aux_addToBlocksList(&free_blocks[order_from_size(block->getSize(cookie))], block);
        block->setIsFree(cookie, true);
    }

    void aux_removeFromFreeBlocks(MallocMetadata* block) {
        int order = order_from_size(block->getSize(cookie));
        if (order < 0) {
#ifdef DEBUG
            std::cout << "Attempted to remove block with illegal size: " << block->getSize(cookie) << "std::endl";
#endif
            return;
        }
        aux_removeFromBlocksList(block, &free_blocks[order]);
    }

    //Buddy here can be fetches by this function, but can also be passed as a parameter for efficiency.
    void aux_mergeStep(MallocMetadata** block_ptr, MallocMetadata* buddy=nullptr) {
        auto block = *block_ptr;
        if (!buddy) {
            buddy = aux_getBuddy(block);

            if (buddy == nullptr || !buddy->getIsFree(cookie)) {
#ifdef DEBUG
                std::cout << "Called aux_mergeStep with non-mergeable blocks.";
#endif
                return;
            }
        }
        auto left_buddy = block < buddy ? block : buddy;
        auto right_buddy = block < buddy ? buddy : block; //Could do sum - min, but not sure if sum might overflow...
        block = left_buddy;
        buddy = right_buddy;
        block->addToSize(cookie, buddy->getSize(cookie));
        auto &free_list = free_blocks[order_from_size(buddy->getSize(cookie))]; //I hope references don't take up heap storage...
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
        size_t block_size = overwrite_size ? overwrite_size : block->getSize(cookie);
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

        bool left = (((long)block - (long)base_heap_addr) / block_size) % 2 == 0;
        auto buddy = (MallocMetadata*)(left ? ((char*)block + block_size) : ((char*)block - block_size));
        if (!buddy->getIsFree(cookie) || buddy->getSize(cookie) != block_size)
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

    /*
     * Seems like rand() only returns up to RAND_MAX which isn't guaranteed to utilize all of an int32's bits,
     * so this generates a 32-bit one in a weird and hacky way. I tried ¯\_('^')_/¯
     */
    int aux_randomizeInt32() {
        int val = 0;

        for (int cnt = 0; cnt < 32; ++cnt) {
            val = val | ((rand() % 2) << cnt);
        }

        return val;
    }

    bool isMemoryMapped(MallocMetadata* block) {
        return block->getSize(cookie) > order_map[MAX_ORDER];
    }

    size_t getBlockSize(const MallocMetadata* const block) {
        return block->getSize(cookie);
    }

    bool isBlockFree(const MallocMetadata* const block) const {
        return block->getIsFree(cookie);
    }

    void initialize_blocks() {
        if (initialized) { return; }
        initialized = true;

#ifdef DEBUG
        std::cout << "Initializing buddy allocator." << std::endl;
#endif

        srand(time(nullptr));
        cookie = aux_randomizeInt32();

        free_blocks[ORDER_COUNT - 1] = base_heap_addr = (MallocMetadata*)sbrk(BLOCK_COUNT * order_map[MAX_ORDER]);

        auto first_block = free_blocks[ORDER_COUNT - 1];
        *first_block = MallocMetadata(order_map[ORDER_COUNT - 1], true, nullptr, nullptr, cookie);
        first_block->setNext(cookie, aux_getBlockByAddressTraversal(MAX_ORDER, 1));
        auto last_block = aux_getBlockByAddressTraversal(MAX_ORDER, BLOCK_COUNT - 1);
        *last_block = MallocMetadata(order_map[ORDER_COUNT - 1], true, nullptr, nullptr, cookie);
        last_block->setPrev(cookie, aux_getBlockByAddressTraversal(MAX_ORDER, BLOCK_COUNT - 2));

        for (long unsigned int i = 1; i < BLOCK_COUNT - 1; ++i) {
            auto curr = aux_getBlockByAddressTraversal(MAX_ORDER, i);
            *curr = MallocMetadata(order_map[ORDER_COUNT - 1], true, nullptr, nullptr, cookie);

            curr->setPrev(cookie, aux_getBlockByAddressTraversal(MAX_ORDER, i - 1));
            curr->setNext(cookie, aux_getBlockByAddressTraversal(MAX_ORDER, i + 1));
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
                if (curr->getIsFree(cookie)) {
                    return curr;
                }
                curr = curr->getNext(cookie);
            }
        }
        return nullptr;
    }

    //TESTING STUFF:
    void TEST_print_orders();
    void TEST_print_blocks();
    void TEST_minimal_matching_no_split();

    MallocMetadata *allocateBlock(size_t size, int count=-1);
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
    free_space += block->getSize(cookie) - sizeof(MallocMetadata);
    ++free_block_count;

    //Remove from used blocks list:
    aux_removeFromBlocksList(block);
    aux_addToFreeBlocks(block);

    //Merge:
    MallocMetadata* buddy;
    while ((buddy = aux_getBuddy(block)) != nullptr
           && (requested_size <= 0 || requested_size > block->getSize(cookie) - sizeof(MallocMetadata))) {
        if (!buddy->getIsFree(cookie)) {
            break;
        }
        aux_mergeStep(&block, buddy);
    }

    return block;
}

void BuddyAllocator::setBlockFree(MallocMetadata *block, bool free_value, size_t requested_size) {
    if (free_value == block->getIsFree(cookie)) {
#ifdef DEBUG
        std::cout << "WARNING: Attempting to free a block that was already freed!" << std::endl;
#endif
        return;
    }

    if (isMemoryMapped(block)) {
        aux_removeFromBlocksList(block);
        auto size = block->getHugepageAlignedSize(cookie);
        allocated_space -= size - sizeof(MallocMetadata);
        --total_allocated_blocks;
        if (munmap(block, size) == -1) {
        #ifdef DEBUG
            std::cout << "munmap failed." << std::endl;
        #endif
        }
        return;
    }

    if (free_value) {
        block = performMerge(block);
    }
    else {
        free_space -= block->getSize(cookie) - sizeof(MallocMetadata);
        --free_block_count;
        aux_removeFromFreeBlocks(block);
        aux_addToBlocksList(&used_blocks, block);

        //Split:
        while (!(
                order_from_size(block->getSize(cookie)) <= 0 //Got to minimal order, or
                || requested_size > ((block->getSize(cookie) / 2) - sizeof(MallocMetadata)) //any smaller is too small
        )) {
            auto buddy = block->split(cookie);
            ++free_block_count;
            ++total_allocated_blocks;
            free_space += buddy->getSize(cookie) - sizeof(MallocMetadata);
            allocated_space -= sizeof(MallocMetadata);
            aux_addToFreeBlocks(buddy);
        }
    }
    block->setIsFree(cookie, free_value);
}

MallocMetadata *BuddyAllocator::allocateBlock(size_t size, int count) {
    initialize_blocks();

    if (size == 0 || size > 100000000) return nullptr;

    bool is_scalloc = count > 0, hugepage = MallocMetadata::isHugepageSized(count <= 0 ? size : size*count, count <= 0 ? 0 : size);
    #ifdef DEBUG
    if (hugepage) std::cout << "Allocating hugepage." << std::endl;
    #endif

    MallocMetadata* block = nullptr;
    if (size + sizeof(MallocMetadata) >= order_map[MAX_ORDER]) { //We were instructed to only handle over 128KiB or under 128KiB-sizeof(MallocMetadata) – not anything inbetween. Still covering it just in case.
        block = (MallocMetadata*)mmap(nullptr, (is_scalloc ? size*count : size) + sizeof(MallocMetadata), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | (hugepage ? MAP_HUGETLB : 0), -1, 0);
        if (block == MAP_FAILED) {
            block = nullptr;
        }
        if (block) {
            *block = !is_scalloc
                    ? MallocMetadata(size + sizeof(MallocMetadata), false, nullptr, nullptr, cookie)
                    : MallocMetadata(size*count + sizeof(MallocMetadata), false, nullptr, nullptr, cookie, size);
            aux_addToBlocksList(&mmapped_blocks, block);
            ++total_allocated_blocks;
            allocated_space += size;
        }
    }
    else {
        block = getMinimalMatchingFreeBlock(size);
        if (block)
            setBlockFree(block, false, size);
    }

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
    auto new_block = performMerge(block, size);
    setBlockFree(new_block, false, size);
    return new_block;
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
    auto addr = allocator.allocateBlock(size, num);
    if (addr == nullptr) {
        return nullptr;
    }
    ++addr;

    std::memset((void*)addr, 0, num * size);

    return addr;
}

void sfree(void* p) {
    if (p == nullptr) return;

    auto pointer = (MallocMetadata*)p;
    --pointer; //To make it point to the metadata

    if (allocator.isBlockFree(pointer)) {
        return;
    }

    allocator.setBlockFree(pointer, true);
}

void *srealloc(void* oldp, size_t size) {
    if (oldp == nullptr) {
        return smalloc(size);
    }
    auto old_block = (MallocMetadata*)oldp;
    --old_block;

    // We were told to assume realloc would only happen between mmap-sized to mmap-sized
    // or non-map-sized to non-mmap-sized. Handling in accordance.

    auto old_size = allocator.getBlockSize(old_block);
    MallocMetadata* newp{nullptr};
    bool in_place{false};

    if (allocator.isMemoryMapped(old_block)) {
        if (size == old_size) {
            return oldp;
        }
    }
    else {
        if (size <= old_size - sizeof(MallocMetadata)) {
            return oldp;
        }

        newp = allocator.attemptInPlaceRealloc(old_block, size);
        if (newp) {
            ++newp;
            in_place = true;
        }
    }

    if (!newp) {
        newp = (MallocMetadata*)smalloc(size);
        if (!newp) {
            return nullptr;
        }
    }

    std::memmove(newp, oldp, old_size);
    if (!in_place) {
        allocator.setBlockFree(old_block, true);
    }

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
    int j;
    std::cout << "Free blocks:" << std::endl;
    for (int i = 0; i < ORDER_COUNT; ++i) {
        std::cout << "@ @ @\nIterating over free_blocks of order " << i << " (size: " <<
                  order_map[i] << " bytes)." << std::endl;
        auto list = free_blocks[i];
        j = 0;
        while (list != nullptr) {
            std::cout << "Block #" << j++ << ": addr=" << list << ", size=" << list->getSize(cookie)
                      << ", " << (list->getIsFree(cookie) ? "" : "not ") << "free.\nBuddy is "
                      << aux_getBuddy(list) << std::endl;
            list = list->getNext(cookie);
        }
    }

    MallocMetadata* lists[] = {used_blocks, mmapped_blocks};
    for (auto list : lists) {
        std::cout << "\nUsed blocks, " << (list == mmapped_blocks ? "" : "non-") << "memory mapped:" << std::endl;
        j = 0;
        while (list != nullptr) {
            std::cout << "Block #" << j++ << ": addr=" << list << ", size=" << list->getSize(cookie)
                      << ", " << (list->getIsFree(cookie) ? "" : "not ") << "free.\n" << std::endl;
            list = list->getNext(cookie);
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
            total_bytes += list->getSize(cookie);
            total_bytes_without_metadata += list->getSize(cookie) - sizeof(MallocMetadata);
            list = list->getNext(cookie);
        }
    }

    if (bytes) *bytes = total_bytes;
    if (bytesWithoutMetadata) *bytesWithoutMetadata = total_bytes_without_metadata;

    return cnt;
}

int BuddyAllocator::aux_full_fetch_of_used_blocks(int *bytes, int *bytesWithoutMetadata) {
    int total_bytes = 0, total_bytes_without_metadata = 0;
    int cnt = 0;
    MallocMetadata* lists[] = {used_blocks, mmapped_blocks};
    for (auto list : lists) {
        auto curr = list;
        while (curr) {
            total_bytes += curr->getSize(cookie);
            total_bytes_without_metadata += curr->getSize(cookie) - sizeof(MallocMetadata);
            ++cnt;
            curr = curr->getNext(cookie);
        }
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