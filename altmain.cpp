#include "altmain.h"

using std::cout;
using std::cin;
using std::endl;

bool statistics_sanity_assertion() {
    if (_num_allocated_blocks() == 0 && _num_free_blocks() == 0) {
        //Pre-initialization, probably. (Will make sure in my use cases.)
        return true;
    }
    bool valid = true;
    assert(valid = valid && (FULL_allocated_blocks_count() == FULL_free_blocks_count() + FULL_used_blocks_count()));
    assert(valid = valid && (FULL_allocated_blocks_bytes_with_metadata() == FULL_free_blocks_bytes_with_metadata() + FULL_used_blocks_bytes_with_metadata()));
    assert(valid = valid && (FULL_free_blocks_bytes() == _num_free_bytes()));
    assert(valid = valid && (FULL_free_blocks_count() == _num_free_blocks()));
    assert(valid = valid && (FULL_allocated_blocks_count() == _num_allocated_blocks()));
    assert(valid = valid && (FULL_allocated_blocks_bytes() == _num_allocated_bytes()));
    assert(valid = valid && (FULL_allocated_blocks_bytes_with_metadata() == FULL_allocated_blocks_bytes() + FULL_metadata_bytes()));
    assert(valid = valid && (FULL_metadata_bytes() == _num_meta_data_bytes()));
    return valid;
}

void sanity_assertion() {
#ifdef DEBUG
    bool valid;
    if (_num_allocated_blocks() == 0 && _num_free_blocks() == 0) {
        //Pre-initialization, probably. (Will make sure in my use cases.)
        valid = true;
    }
    else {
        valid = _num_meta_data_bytes() == (_num_allocated_blocks() * _size_meta_data());
        assert(_num_meta_data_bytes() == (_num_allocated_blocks() * _size_meta_data()));

        valid = valid && (_num_allocated_bytes() <= _num_allocated_blocks() * 128 * 1024);
        assert(_num_allocated_bytes() <= _num_allocated_blocks() * 128 * 1024);

        valid = valid && (_num_allocated_blocks() >= 32);
        assert(_num_allocated_blocks() >= 32);

        valid = valid && (_num_free_bytes() <= _num_free_blocks() * 128 * 1024);
        assert(_num_free_bytes() <= _num_free_blocks() * 128 * 1024);

        valid = valid && (_num_free_blocks() <= _num_allocated_blocks());
        assert(_num_free_blocks() <= _num_allocated_blocks());

        valid = valid && statistics_sanity_assertion();
    }
    cout << "Sanity check "
         << (valid ? "passed" : "!!!!!!!!!!!!!!!!!!!!!!failed!!!!!!!!!!!!!!!!!!!!!!")
         << "." << endl;
#endif
}

void print_stats(const char* after_func_name="") {
#ifdef DEBUG
    cout //<< "* * * * * *" << endl
            << after_func_name << (strlen(after_func_name) > 0 ? ":\n" : "")
            << "Free blocks: " << _num_free_blocks() << endl
            << "Free bytes: " << _num_free_bytes() << endl
            << "Total allocated blocks: " << _num_allocated_blocks() << endl
            << "Total allocated bytes: " << _num_allocated_bytes() << endl
            << "Total bytes of metadata: " << _num_meta_data_bytes() << endl
            << "Size of single metadata section: " << _size_meta_data() << endl;
    sanity_assertion();
    cout << "* * * * * *\n" << endl;
#endif
}

int main() {
    std::vector<void*> allocations;

    // Allocate 64 blocks of size 128 * 2^9 - 64
    for (int i = 0; i < 64; i++)
    {
        void* ptr = smalloc(128 * std::pow(2, 9) - 64);
        REQUIRE(ptr != nullptr);
        allocations.push_back(ptr);
//        printf("%d\n",i);
//        fflush(stdout);
        verify_block_by_order(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, allocations.size()%2, allocations.size(), 32-(int)(i/2)-1, 0, 0, 0);
    }

    REQUIRE(smalloc(40) == NULL);
    // Free the allocated blocks
    while (!allocations.empty())
    {
        void* ptr = allocations.back();
        allocations.pop_back();
        sfree(ptr);
        verify_block_by_order(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, allocations.size() % 2, allocations.size(), 32 - (int)(allocations.size() / 2) -(allocations.size() % 2), 0, 0, 0);
    }

    // Verify that all blocks are merged into a single large block
    verify_block_by_order(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 0, 0, 0);


    for (int i = 0; i < 64; i++)
    {
        void* ptr = smalloc(128 * std::pow(2, 9) - 64);
        REQUIRE(ptr != nullptr);
        allocations.push_back(ptr);
        verify_block_by_order(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, allocations.size()%2, allocations.size(), 32-(int)(i/2)-1, 0, 0, 0);
    }
    REQUIRE(smalloc(40) == NULL);
    // Free the allocated blocks
    while (!allocations.empty())
    {
        void* ptr = allocations.front();
        allocations.erase(allocations.begin());
        sfree(ptr);
        verify_block_by_order(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, allocations.size() % 2, allocations.size(), 32 - (int)(allocations.size() / 2) -(allocations.size() % 2), 0, 0, 0);
    }
    verify_block_by_order(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 0, 0, 0);

    return 0;
}
