#include <iostream>
#include <cstring>
#include <stdio.h>
#include <unistd.h>
#include <cmath>
#include <assert.h>

using namespace std;

struct MallocMetadata {
    size_t size;
    bool is_free;
    struct MallocMetadata* next;
    struct MallocMetadata* prev;
};

struct MallocMetadata* init = NULL;

/* Searches for a free block with at least‘size’ bytes or allocates (sbrk()) one if none
are found.
Return value:
i. Success – returns pointer to the first byte in the allocated block (excluding the
meta-data of course)
ii. Failure –
a. If size is 0 returns NULL.
b. If ‘size’ is more than 10^8
c. If sbrk fails in allocating the needed space, return NULL.*/

void* smalloc(size_t size) {
    if(size <= 0 || size > pow(10,8)) {
        return NULL;
    }
    
    if(init == NULL) {
        //then allocate
        MallocMetadata* program_break = (MallocMetadata*)sbrk(sizeof(MallocMetadata) + size);
        if(program_break == (void*)(-1)) {
            return NULL;
        }   
        init = program_break;
        init->size = size;
        init->is_free = false;
        init->prev = NULL;
        init->next = NULL;
        return (void*)((char*)program_break + sizeof(MallocMetadata));
    }
    
    //start from the first meta data and try to find enough free space
    MallocMetadata* current_block = init;
    MallocMetadata* prev = current_block;
    while(current_block != NULL) {
        if((current_block->is_free == true) && (current_block->size >= size)) {
            current_block->is_free = false;
            return (void*)((char*)current_block + sizeof(MallocMetadata));
        }
        prev = current_block;
        current_block = current_block->next;
    }  

    //if didn't find a free block then allocate in heap by sbrk
    MallocMetadata* new_program_break = (MallocMetadata*)sbrk(size + sizeof(MallocMetadata));
    if(new_program_break == (void*)(-1)) {
        return NULL;
    } 
    
    new_program_break->size = size;
    new_program_break->is_free = false;
    new_program_break->prev = prev;
    new_program_break->next = NULL; 
    prev->next = new_program_break;          
    return (void*)((char*)new_program_break + sizeof(MallocMetadata));
}


/* Searches for a free block of at least ‘num’ elements, each ‘size’ bytes that are all set to
0 or allocates if none are found. In other words, find/allocate size * num bytes and set
all bytes to 0.
Return value:
i. Success - returns pointer to the first byte in the allocated block.
ii. Failure –
a. If size or num is 0 returns NULL.
b. If ‘size * num’ is more than 10^8
, return NULL.
c. If sbrk fails in allocating the needed space, return NULL.*/

void* scalloc(size_t num, size_t size){
    if(num <= 0 || size <= 0 || size*num > pow(10,8)) {
        return NULL;
    }
    void* allocated = smalloc(num * size);
    if(allocated == NULL) {
        return NULL;
    }
    std::memset(allocated, 0, num * size);
    return allocated;
}

/*return meta data from pointer */
MallocMetadata* getMallocMetadataFromPointer(void* ptr) {
    if(ptr == NULL) {
        return NULL;
    }
    return (MallocMetadata*)((char*)ptr - sizeof(MallocMetadata));
}

/* Releases the usage of the block that starts with the pointer ‘p’.
If ‘p’ is NULL or already released, simply returns.
Presume that all pointers ‘p’ truly points to the beginning of an allocated block. */
void sfree(void* p) {
    if(p == NULL) {
        return;
    }
    MallocMetadata* meta_ptr = getMallocMetadataFromPointer(p);
    assert(meta_ptr != NULL);
    meta_ptr->is_free = true;
    return;
}

/* If ‘size’ is smaller than or equal to the current block’s size, reuses the same block.
Otherwise, finds/allocates ‘size’ bytes for a new space, copies content of oldp into the
new allocated space and frees the oldp.
Return value:
i. Success –
a. Returns pointer to the first byte in the (newly) allocated space.
b. If ‘oldp’ is NULL, allocates space for ‘size’ bytes and returns a pointer to it.
ii. Failure –
a. If size is 0 returns NULL.
b. If ‘size’ if more than 10^8
, return NULL.
c. If sbrk fails in allocating the needed space, return NULL.
d. Do not free ‘oldp’ if srealloc() fails.*/
void* srealloc(void* oldp, size_t size){
    if(size > pow(10,8) || size <= 0) {
        return NULL;
    }    
    if(oldp != NULL){
        MallocMetadata* meta_ptr = getMallocMetadataFromPointer(oldp);
        assert(meta_ptr != NULL);
        //check if there's room in the same block
        if(meta_ptr->size >= size) {
            return oldp;
        }
        //otherwise find newspace
        void * new_block = smalloc(size);
        if(new_block == NULL){
            return NULL;
        }  
        memmove(new_block, oldp, meta_ptr->size);
        sfree(oldp);
        return new_block;
    }
    //oldp == null then allocate space
    void * new_block = smalloc(size);
    if(new_block == NULL){
        return NULL;
    }
    return new_block;
}

/* Returns the number of allocated blocks in the heap that are currently free.*/ 
size_t _num_free_blocks() {
    size_t counter_free = 0;
    MallocMetadata* current_block = init;
    while(current_block != NULL) {
        if(current_block->is_free == true) {
            counter_free++;
        }
        current_block = current_block->next;
    } 
    return counter_free;
}

/* Returns the number of bytes in all allocated blocks in the heap that are currently free,
excluding the bytes used by the meta-data structs. */
size_t _num_free_bytes() {
    size_t number_of_free_bytes = 0;
    MallocMetadata* current_block = init;
    while(current_block != NULL) {
        if(current_block->is_free == true) {
            number_of_free_bytes += current_block->size;
        }
        current_block = current_block->next;
    } 
    return number_of_free_bytes;
}

/* Returns the overall (free and used) number of allocated blocks in the heap. */
size_t _num_allocated_blocks() {
    size_t counter = 0;
    MallocMetadata* current_block = init;
    while(current_block != NULL) {
        counter++;
        current_block = current_block->next;
    } 
    return counter;
}

/* Returns the overall number (free and used) of allocated bytes in the heap, excluding
the bytes used by the meta-data structs. */
size_t _num_allocated_bytes() {
    size_t number_of_bytes = 0;
    MallocMetadata* current_block = init;
    while(current_block != NULL) {
        number_of_bytes += current_block->size;
        current_block = current_block->next;
    } 
    return number_of_bytes;  
}

/* Returns the number of bytes of a single meta-data structure in your system. */
size_t _size_meta_data() {
    return sizeof(MallocMetadata);
}

/* Returns the overall number of meta-data bytes currently in the heap. */
size_t _num_meta_data_bytes() {    
    size_t counter_of_blocks = _num_allocated_blocks();
    return counter_of_blocks * _size_meta_data();
}


// void test_srealloc_simple(){
//     cout << "======start simple test======\n" << endl;
//     void* program_break = sbrk(0);
//     cout << "the initial program break:" << (long int*)program_break << endl;
//     void* ptr = smalloc(10);
//     cout << "the ptr to allocated block of 10 bytes (without meta data) is:" << (long int*)ptr << endl;
//     assert((char*)ptr - (char*)program_break == sizeof(MallocMetadata));
//     program_break = sbrk(0);
//     cout << "the program break after allocating 10 bytes is:" << (long int*)program_break << endl;
//     assert((char*)program_break - (char*)ptr == 10);
//     void* new_p = srealloc(ptr,10);
//     assert(new_p == ptr);
//     new_p = srealloc(ptr,20);
//     cout << "the ptr to allocated block of 20 bytes (without meta data) is:" << (long int*)new_p << endl;
//     MallocMetadata* old_meta_data = getMallocMetadataFromPointer(ptr);
//     assert(old_meta_data->is_free == true);
//     ptr = srealloc(new_p, 5);
//     assert(new_p == ptr);
//     program_break = sbrk(0);
//     cout << "the program break before allocating 16 bytes is:" << (long int*)program_break << endl;
//     void* new_new_ptr = srealloc(NULL, 16);
//     cout << "the ptr to allocated block of 16 bytes (without meta data) is:" << (long int*)new_new_ptr << endl;
//     assert((char*)new_new_ptr - (char*)program_break == sizeof(MallocMetadata));
//     program_break = sbrk(0);
//     cout << "the program break after allocating 16 bytes is:" << (long int*)program_break << endl;
//     assert((char*)program_break - (char*)new_new_ptr == 16);
//     cout << "\n======passed simple test======\n" << endl;
//     return;
// }

// void test_smalloc_simple(){
//     cout << "======start simple test2======\n" << endl;
//     void* program_break = sbrk(0);
//     cout << "the initial program break:" << (long int*)program_break << endl;
//     void* ptr1 = smalloc(16);
//     cout << "the ptr to allocated block of 16 bytes (without meta data) is:" << (long int*)ptr1 << endl;
//     assert((char*)ptr - (char*)program_break == sizeof(MallocMetadata));
//     program_break = sbrk(0);
//     cout << "the program break after allocating 16 bytes is:" << (long int*)program_break << endl;
//     assert((char*)program_break - (char*)ptr == 16);
//     void* ptr2 = smalloc(48);
//     cout << "the ptr to allocated block of 48 bytes (without meta data) is:" << (long int*)ptr2 << endl;
//     assert((char*)ptr2 - (char*)program_break == sizeof(MallocMetadata));
//     program_break = sbrk(0);
//     cout << "the program break after allocating 48 bytes is:" << (long int*)program_break << endl;
//     assert((char*)program_break - (char*)ptr == 48);
//     void* ptr3 = scalloc(3,16);
//     cout << "the ptr to allocated block of 3*16 bytes(without meta data) with scalloc is:" << (long int*)ptr3 << endl;
//     assert((char*)ptr3 - (char*)program_break == sizeof(MallocMetadata));
//     program_break = sbrk(0);
//     cout << "the program break after allocating 3*16 bytes is:" << (long int*)program_break << endl;
//     assert((char*)program_break - (char*)ptr == 48);
//     sfree(ptr1);
//     sfree(ptr2);
//     ptr1 = smalloc(8);
//     cout << "\n======passed simple test======\n" << endl;
//     return;
// }

// void test_error(){
//     cout << "======start error test======\n" << endl;
//     void* program_break = smalloc(0);
//     assert(program_break==NULL);
//     program_break = smalloc(pow(10,8)+1);
//     assert(program_break==NULL);
//     program_break = scalloc(0,10);
//     assert(program_break==NULL);
//     program_break = scalloc(10,0);
//     assert(program_break==NULL);
//     program_break = scalloc(1,pow(10,8)+1);
//     assert(program_break==NULL);
//     program_break = srealloc(program_break,pow(10,8)+1);
//     assert(program_break==NULL);
//     program_break = srealloc(program_break,0);
//     assert(program_break==NULL);
//     cout << "\n======passed error test======\n" << endl;
//     return;
// }

// void test_stats(){
//     cout << "======start stat======\n" << endl;
//     void* program_break = sbrk(0);
//     cout << "the initial program break:" << (long int*)program_break << endl;
//     void* ptr = smalloc(16);

//     return;
// }

// int main(){
//     test_error();
//     test_srealloc_simple();
//     test_smalloc_simple();   
//     return 0;
// }