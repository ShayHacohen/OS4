//
// Created by ShayH on 04/07/2023.
//

#ifndef SOL_ALTMAIN_H
#define SOL_ALTMAIN_H

#define REQUIRE assert

#include <iostream>
#include <cstring>
#include <cassert>
#include <vector>
#include <cmath>

void *srealloc(void* oldp, size_t size);
void sfree(void* p);
void* scalloc(size_t num, size_t size);
void* smalloc(size_t size);

size_t _num_free_blocks();
size_t _num_free_bytes();
size_t _num_allocated_blocks();
size_t _num_allocated_bytes();
size_t _num_meta_data_bytes();
size_t _size_meta_data();
int FULL_free_blocks_count();
int FULL_free_blocks_bytes();
int FULL_free_blocks_bytes_with_metadata();
int FULL_used_blocks_count();
int FULL_used_blocks_bytes();
int FULL_used_blocks_bytes_with_metadata();
int FULL_allocated_blocks_count();
int FULL_allocated_blocks_bytes();
int FULL_allocated_blocks_bytes_with_metadata();
int FULL_metadata_bytes ();
void TEST_print_orders();
void TEST_print_blocks();
void TEST_several_stuff();


void performCorruption() {
    // Allocate memory
    void* ptr1 = smalloc(16);  // Allocate 16 bytes
    REQUIRE(ptr1 != nullptr);
    void* ptr2 = smalloc(32);  // Allocate 32 bytes
    REQUIRE(ptr2 != nullptr);

    // Overflow the first allocation
    char* overflowPtr = reinterpret_cast<char*>(ptr1);
    for (int i = 0; i < 2000; i++) {
        overflowPtr[i] = 'A';
    }

    // Allocate more memory
    void* ptr3 = smalloc(64);  // Allocate 64 bytes
    REQUIRE(ptr3 != nullptr);

    // Free the allocations
    sfree(ptr1);
    sfree(ptr2);
    sfree(ptr3);

}
#define MAX_ALLOCATION_SIZE (1e8)
#define MMAP_THRESHOLD (128 * 1024)
#define MIN_SPLIT_SIZE (128)
#define MAX_ELEMENT_SIZE (128*1024)
//static inline size_t aligned_size(size_t size)
//{
//    return (size % 8) ? (size & (size_t)(-8)) + 8 : size;
//}

#define verify_blocks(allocated_blocks, allocated_bytes, free_blocks, free_bytes)                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        REQUIRE(_num_allocated_blocks() == allocated_blocks);                                                          \
        REQUIRE(_num_allocated_bytes() == (allocated_bytes));                                              \
        REQUIRE(_num_free_blocks() == free_blocks);                                                                    \
        REQUIRE(_num_free_bytes() == (free_bytes));                                                        \
        REQUIRE(_num_meta_data_bytes() == (_size_meta_data() * allocated_blocks));                         \
    } while (0)

#define verify_size(base)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        void *after = sbrk(0);                                                                                         \
        REQUIRE(_num_allocated_bytes() + aligned_size(_size_meta_data() * _num_allocated_blocks()) ==                  \
                (size_t)after - (size_t)base);                                                                         \
    } while (0)

#define verify_size_with_large_blocks(base, diff)                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        void *after = sbrk(0);                                                                                         \
        REQUIRE(diff == (size_t)after - (size_t)base);                                                                 \
    } while (0)
void verify_block_by_order(int order0free, int order0used, int order1free, int order1used, \
                                int order2free, int order2used,\
                                int order3free, int order3used, \
                                int order4free, int order4used, \
                                int order5free, int order5used, \
                                int order6free, int order6used, \
                                int order7free, int order7used, \
                                int order8free,int  order8used, \
                                int order9free,int  order9used, \
                                int order10free,int  order10used,
                           int big_blocks_count, long big_blocks_size  )\
                                                                                                                     \
    {                                                                                                                  \
        unsigned int __total_blocks = order0free + order0used+ order1free + order1used+ order2free + order2used+ order3free + order3used+ order4free + order4used+ order5free + order5used+ order6free + order6used+ order7free + order7used+ order8free + order8used+ order9free + order9used+ order10free + order10used + big_blocks_count       ;        \
        unsigned int __total_free_blocks = order0free+ order1free+ order2free+ order3free+ order4free+ order5free+ order6free+ order7free+ order8free+ order9free+ order10free ;                     \
        unsigned int __total_free_bytes_with_meta  = order0free*128*pow(2,0) +  order1free*128*pow(2,1) +  order2free*128*pow(2,2) +  order3free*128*pow(2,3) +  order4free*128*pow(2,4) +  order5free*128*pow(2,5) +  order6free*128*pow(2,6) +  order7free*128*pow(2,7) +  order8free*128*pow(2,8) +  order9free*128*pow(2,9)+  order10free*128*pow(2,10) ;                                                                     \
        unsigned int testing_allocated_bytes;
    if (__total_blocks==0) testing_allocated_bytes = 0;
    else testing_allocated_bytes = big_blocks_size+32 * MAX_ELEMENT_SIZE - (__total_blocks-big_blocks_count)*(_size_meta_data());
    verify_blocks(__total_blocks, testing_allocated_bytes, __total_free_blocks,__total_free_bytes_with_meta - __total_free_blocks*(_size_meta_data()));\
    }

#endif //SOL_ALTMAIN_H
