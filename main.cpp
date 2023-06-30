#include <iostream>
#include <cstring>

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

void print_stats(const char* after_func_name="") {
#ifdef DEBUG
    cout //<< "* * * * * *" << endl
        << after_func_name << (strlen(after_func_name) > 0 ? ":\n" : "")
        << "Free blocks: " << _num_free_blocks() << endl
        << "Free bytes: " << _num_free_bytes() << endl
        << "Total allocated blocks: " << _num_allocated_blocks() << endl
        << "Total allocated bytes: " << _num_allocated_bytes() << endl
        << "Total bytes of metadata: " << _num_meta_data_bytes() << endl
        << "Size of single metadata section: " << _size_meta_data() << endl
        << "* * * * * *\n" << endl;
#endif
}

int main() {
    char *str = (char*)smalloc(strlen("hello") + 1), *orig;
    sprintf(str, "hello");

    cout << "scalloc: " << str << endl;
    print_stats();

    str = (char*)srealloc(str, strlen("hello") + 1);
    cout << "srealloc size unchanged: " << str << endl;
    print_stats();

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
    return 0;
}
