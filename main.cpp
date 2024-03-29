#include <iostream>
#include <cstring>
#include <cassert>
#include <unistd.h>

using std::cout;
using std::cin;
using std::endl;

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
    print_stats("Before any function call");

    char *str = (char*)smalloc(strlen("hello") + 1), *orig;
    sprintf(str, "hello");
    print_stats("smalloc");

    TEST_print_blocks();

    sfree(str);
    print_stats("sfree");

    str = (char*)smalloc(strlen("hello") + 1);
    print_stats("smalloc");

    str = (char*)srealloc(str, strlen("hello") + 1);
    //sprintf(str, "hello");
    cout << "srealloc size unchanged: " << str << endl;
    print_stats("srealloc");

    str = (char*)srealloc(str, strlen("hello") + 10);
    cout << "srealloc size changed: " << str << endl;
    print_stats();

    orig = str;
    sfree(str);
    print_stats("sfree");

    str = (char*)scalloc(strlen("hello") + 10, sizeof(char));
    print_stats("scalloc");
    cout << "sfree & then scalloc; got " << (orig == str ? "same" : "different") << " address" << endl;
    if (orig != str) {
        cout << ": " << "orig == " << (void*)orig << ", str == " << (void*)str << endl;
    }

    auto mmapped = smalloc(128 * 1024 + 1);
    print_stats("mmap-smalloc");
    sprintf((char*)mmapped, "mmap testing wooo\n@@@@@@@@@@@@@@@@@@@@@@@@@\n@@@@@@@@@@@@@@@@@@@@@@@@@\n@@@@@@@@@@@@@@@@@@@@@@@@@\n");
    cout << "mmapped contents: " << (char*)mmapped << endl;

    TEST_print_orders();
    TEST_several_stuff();
    TEST_print_blocks();

    sfree(mmapped);
    print_stats("sfree");
    TEST_print_blocks();

    cout << "1 (NOT)" << endl;
    auto not_hugepaged_by_smalloc_1 = smalloc(1024 * 1024 * 4);
    cout << "2 (NOT)" << endl;
    auto not_hugepaged_by_smalloc_2 = smalloc(1024 * 1024 * 4 + 1);
    cout << "3 (NOT)" << endl;
    auto not_hugepaged_by_smalloc_3 = smalloc(1024 * 1024 * 4 - 1);
    cout << "4 (NOT)" << endl;
    auto not_hugepaged_by_smalloc_4 = smalloc(1024 * 1024 * 4 + _size_meta_data() - 1);
    cout << "5 (YES)" << endl;
    auto hugepaged_by_smalloc = smalloc(1024 * 1024 * 4 + _size_meta_data());
    cout << "6 (NOT)" << endl;
    auto not_hugepaged_by_scalloc_1 = scalloc(5, 1024 * 1024 * 2);
    cout << "7 (YES)" << endl;
    auto hugepaged_by_scalloc_1 = scalloc(2, 1024 * 1024 * 2 + 1);
    cout << "8 (NOT)" << endl;
    auto not_hugepaged_by_scalloc_2 = scalloc(3, 1024 * 1024 * 2 - 1);
    cout << "9 (YES)" << endl;
    auto hugepaged_by_scalloc_2 = scalloc(1, 1024 * 1024 * 2 + 1);

    cout << "sfree 1: SHOULD NOT COMPLAIN." << endl;
    sfree(hugepaged_by_scalloc_1);
    cout << "sfree 2: SHOULD NOT COMPLAIN." << endl;
    sfree(hugepaged_by_smalloc);
    cout << "sfree 3: SHOULD NOT COMPLAIN." << endl;
    sfree(hugepaged_by_scalloc_2);


    return 0;
}
