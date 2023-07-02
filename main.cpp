#include <iostream>
#include <cstring>
#include <cassert>

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
void TEST_print_orders();
void TEST_print_blocks();
void TEST_several_stuff();
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

        valid = valid && (_num_allocated_bytes() + _num_meta_data_bytes() == 128 * 1024 * 32);
        assert(_num_allocated_bytes() + _num_meta_data_bytes() == 128 * 1024 * 32);
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
        << "Total allocated free_blocks: " << _num_allocated_blocks() << endl
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

    /*str = (char*)scalloc(strlen("hello") + 10, sizeof(char));
    print_stats("scalloc");
    cout << "sfree & then scalloc; got " << (orig == str ? "same" : "different") << " address" << endl;
    if (orig != str) {
        cout << ": " << "orig == " << (void*)orig << ", str == " << (void*)str << endl;
    }

    //TEST_print_orders();
    TEST_print_blocks();
    TEST_several_stuff();*/
    return 0;
}
