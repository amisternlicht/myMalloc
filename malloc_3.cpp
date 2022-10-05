#include <iostream>
#include <cstring>
#include <stdio.h>
#include <unistd.h>
#include <cmath>
#include <assert.h>
#include <sys/mman.h>

using namespace std;

struct MallocMetadata {
    size_t size;
    bool is_free;
    void* Align; 
    struct MallocMetadata* next;
    struct MallocMetadata* prev;
    struct MallocMetadata* free_next;
    struct MallocMetadata* free_prev;
};

struct MallocMetadata* init = nullptr;
struct MallocMetadata* mmap_list = nullptr;
struct MallocMetadata* free_list = nullptr;


//is alpha larger than beta
bool _largerThan(MallocMetadata* alpha, MallocMetadata* beta){
    assert(alpha != nullptr);
    assert(beta != nullptr);

    if(alpha->size > beta->size) {
        return true;
    }
    if(alpha->size < beta->size) {
        return false;
    }

    // alpha->size == beta->size
    return (alpha > beta);

}

bool isMmapAllocation(size_t size) {
    if(size >= 128*1024) {
        return true;
    }
    return false;
}

bool isMmapAllocation(MallocMetadata* meta_data) {
    assert(meta_data);
    if(meta_data->size >= 128*1024) {
        return true;
    }
    return false;
}

void _insertToFreeList(MallocMetadata* free_meta_data) {
    MallocMetadata* current_block = free_list;
    MallocMetadata* prev = current_block;
    
    //list is empty
    if(free_list == nullptr) {
        free_list = free_meta_data;
        return;
    }

    //smaller than first
    if(_largerThan(current_block, free_meta_data)){
        free_list = free_meta_data;
        free_meta_data->free_next = current_block;
        current_block->free_prev = free_meta_data;
        return;
    }
    
    while(current_block != nullptr) {
        if(_largerThan(current_block, free_meta_data)) {
            free_meta_data->free_prev = prev;
            free_meta_data->free_next = current_block; 
            prev->free_next = free_meta_data; 
            current_block->free_prev = free_meta_data;
            return;
        }
        prev = current_block;
        current_block = current_block->free_next;
    }
    
    //i know i am larger than the last one (prev)
    prev->free_next = free_meta_data;
    free_meta_data->free_prev = prev;
    return;
}

void _deleteFromFreeList(MallocMetadata* free_meta_data) {
    if(free_meta_data == nullptr) {
        return;
    }
    
    MallocMetadata* next = free_meta_data->free_next;
    MallocMetadata* prev = free_meta_data->free_prev;
    free_meta_data->free_next = nullptr;
    free_meta_data->free_prev = nullptr;
    
    // first element in the list
    if(prev == nullptr) {
        free_list = next;
        if(next != nullptr) {
            next->free_prev = nullptr;
        }
        return;
    }
    
    // last element in the list
    if(next == nullptr) {
        prev->free_next = nullptr;
        return;
    }
    
    prev->free_next = next;
    next->free_prev = prev;

    return;
}

MallocMetadata* _unionFromRight(MallocMetadata* free_meta_data) {
    if(free_meta_data->next == nullptr) {
        return free_meta_data;
    }
    
    MallocMetadata* next = free_meta_data->next->next;
    
    if(free_meta_data->next->is_free == true) {
        _deleteFromFreeList(free_meta_data->next);
        if(free_meta_data->is_free == true){
            _deleteFromFreeList(free_meta_data);
        }
        free_meta_data->size += free_meta_data->next->size + sizeof(MallocMetadata);
        free_meta_data->next = next;
        if(next != nullptr) {
            next->prev = free_meta_data;
        }
        _insertToFreeList(free_meta_data);        
    }
    
    return free_meta_data;
}

MallocMetadata* _unionFromLeft(MallocMetadata* free_meta_data) {
    if(free_meta_data->prev == nullptr) {
        return free_meta_data;
    }
    
    if(free_meta_data->prev->is_free == true){
        _deleteFromFreeList(free_meta_data->prev);
        if(free_meta_data->is_free == true){
            _deleteFromFreeList(free_meta_data);
        }
        free_meta_data->prev->size += free_meta_data->size + sizeof(MallocMetadata);
        free_meta_data->prev->next = free_meta_data->next;
        if(free_meta_data->next != nullptr) {
            free_meta_data->next->prev = free_meta_data->prev;
        }
        MallocMetadata* to_add = free_meta_data->prev;
        _insertToFreeList(to_add);        
        return free_meta_data->prev;
    }
    return free_meta_data;
}

MallocMetadata* _searchFreeList(size_t size){
    MallocMetadata* current_block = free_list;
    while(current_block != nullptr) {
        if(current_block->size >= size) {
            return current_block;
        }
        current_block = current_block->free_next;
    }  
    return nullptr;
}

MallocMetadata* _getLast() {
    if(init == nullptr) {
        return nullptr;
    }
    MallocMetadata* current_block = init;
    while(current_block->next != nullptr) {
        current_block = current_block->next;
    }  
    return current_block;
}

void _split(MallocMetadata* meta_data_to_split, size_t size) {
    if(meta_data_to_split == nullptr) {
        return;
    }
    if(((int)meta_data_to_split->size - (int)size - (int)sizeof(MallocMetadata)) >= 128) {
        MallocMetadata* new_meta_data = (MallocMetadata*)((char*)meta_data_to_split + sizeof(MallocMetadata) + size);
        new_meta_data->size = meta_data_to_split->size - size - sizeof(MallocMetadata);
        new_meta_data->is_free = true;
        meta_data_to_split->size = size;

        MallocMetadata* next = meta_data_to_split->next;
        
        meta_data_to_split->next = new_meta_data;
        new_meta_data->prev = meta_data_to_split;
        new_meta_data->next = next;
        if(next != nullptr) {
            next->prev = new_meta_data; 
        }
        new_meta_data->free_next = nullptr;
        new_meta_data->free_prev = nullptr;
        _insertToFreeList(new_meta_data);
    }
}

size_t alignSize(size_t size){
    if(size%8 != 0){
        size += 8 - size%8;
    }
    return size;
}

/* Searches for a free block with at least‘size’ bytes or allocates (sbrk()) one if none
are found.
Return value:
i. Success – returns pointer to the first byte in the allocated block (excluding the
meta-data of course)
ii. Failure –
a. If size is 0 returns nullptr.
b. If ‘size’ is more than 10^8
c. If sbrk fails in allocating the needed space, return nullptr.*/

void* smalloc(size_t size) {
    if(size <= 0 || size > pow(10,8)) {
        return nullptr;
    }
    size = alignSize(size);

    if(isMmapAllocation(size) == true) {
        void* align = (void*)mmap(nullptr, size + sizeof(MallocMetadata),
                                              PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if(align == (void*)(-1)) {
            return nullptr;
        } 
        int align_remainder = 0;
        if((uintptr_t)align%8 != 0) {
            align_remainder = 8 - (uintptr_t)align%8;
        }
        (void*)mmap(align, size + sizeof(MallocMetadata) + align_remainder,
                                              PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if(align == (void*)(-1)) {
            return nullptr;
        } 
        MallocMetadata* mmap_allocation = (MallocMetadata*)align;
        mmap_allocation->Align = align; 
        if(mmap_list == nullptr) {
            mmap_list = mmap_allocation;
            mmap_allocation->prev = nullptr;
            mmap_allocation->next = nullptr;
        }
        else {
            MallocMetadata* first = mmap_list;
            mmap_allocation->next = first;
            mmap_allocation->prev = nullptr;
            first->prev = mmap_allocation;
            mmap_list = mmap_allocation;
        }
        mmap_allocation->size = size;
        mmap_allocation->is_free = false;
        mmap_allocation->free_next = nullptr;
        mmap_allocation->free_prev = nullptr;
        return (void*)((char*)mmap_allocation + sizeof(MallocMetadata));
    }
    
    if(init == nullptr) {
        //then allocate
        MallocMetadata* program_break = (MallocMetadata*)sbrk(sizeof(MallocMetadata) + size);
        program_break -= (uintptr_t)program_break%8;
        if(program_break == (void*)(-1)) {
            return nullptr;
        }   
        init = program_break;
        init->size = size;
        init->is_free = false;
        init->free_next = nullptr;
        init->free_prev = nullptr;
        init->prev = nullptr;
        init->next = nullptr;
        return (void*)((char*)program_break + sizeof(MallocMetadata));
    }
    
    //start from the first meta data and try to find enough free space
    MallocMetadata* new_ptr = _searchFreeList(size);

    if(new_ptr != nullptr) {
        _deleteFromFreeList(new_ptr);
        new_ptr->is_free = false;
        _split(new_ptr, size);    
        return (void*)((char*)new_ptr + sizeof(MallocMetadata));
    }  
      
    MallocMetadata* last = _getLast();
    MallocMetadata* new_program_break;
    //wilderness
    if(last->is_free == true) {
        new_program_break = (MallocMetadata*)sbrk((int)size - last->size);
        if(new_program_break == (void*)(-1)) {
            return nullptr;
        } 
        last->is_free = false;
        last->size = size;
        last->free_next = nullptr;
        last->free_prev = nullptr;
        _deleteFromFreeList(last);
        return (void*)((char*)last + sizeof(MallocMetadata));
    }
    else {   
        //if didn't find a free block then allocate in heap by sbrk
        new_program_break = (MallocMetadata*)sbrk(sizeof(MallocMetadata) + size);
        if(new_program_break == (void*)(-1)) {
            return nullptr;
        } 
        new_program_break->prev = last;
        last->next = new_program_break;          
        new_program_break->size = size;
        new_program_break->is_free = false;
        new_program_break->free_next = nullptr;
        new_program_break->free_prev = nullptr;
        new_program_break->next = nullptr; 
        return (void*)((char*)new_program_break + sizeof(MallocMetadata));
    }

}


/* Searches for a free block of at least ‘num’ elements, each ‘size’ bytes that are all set to
0 or allocates if none are found. In other words, find/allocate size * num bytes and set
all bytes to 0.
Return value:
i. Success - returns pointer to the first byte in the allocated block.
ii. Failure –
a. If size or num is 0 returns nullptr.
b. If ‘size * num’ is more than 10^8
, return nullptr.
c. If sbrk fails in allocating the needed space, return nullptr.*/

void* scalloc(size_t num, size_t size){
    if(num <= 0 || size <= 0 || size*num > pow(10,8)) {
        return nullptr;
    }
    void* allocated = smalloc(num * size);
    if(allocated == nullptr) {
        return nullptr;
    }
    std::memset(allocated, 0, num * size);
    return allocated;
}

/*return meta data from pointer */
MallocMetadata* getMallocMetadataFromPointer(void* ptr) {
    if(ptr == nullptr) {
        return nullptr;
    }
    return (MallocMetadata*)((char*)ptr - sizeof(MallocMetadata));
}

void _removeFromMmapList(MallocMetadata* to_delete){
    if(to_delete == nullptr) {
        return;
    }
    
    MallocMetadata* next = to_delete->next;
    MallocMetadata* prev = to_delete->prev;
    
    // first element in the list
    if(prev == nullptr) {
        mmap_list = next;
        if(next != nullptr) {
            next->prev = nullptr;
        }
        return;
    }
    
    // last element in the list
    if(next == nullptr) {
        prev->next = nullptr;
        return;
    }
    
    prev->next = next;
    next->prev = prev;
 
    return;
}

/* Releases the usage of the block that starts with the pointer ‘p’.
If ‘p’ is nullptr or already released, simply returns.
Presume that all pointers ‘p’ truly points to the beginning of an allocated block. */
void sfree(void* p) {
    if(p == nullptr) {
        return;
    }
    MallocMetadata* meta_ptr = getMallocMetadataFromPointer(p);
    assert(meta_ptr != nullptr);
    if(meta_ptr->size < 128*1024){
        meta_ptr->is_free = true;
        meta_ptr->free_next = nullptr;
        meta_ptr->free_prev = nullptr;
        _insertToFreeList(meta_ptr);
        meta_ptr = _unionFromRight(meta_ptr);
        meta_ptr = _unionFromLeft(meta_ptr);
    }
    else{
        _removeFromMmapList(meta_ptr);
        munmap(meta_ptr->Align, meta_ptr->size + sizeof(MallocMetadata)+(char*)meta_ptr - (char*)meta_ptr->Align);
    }
    return;
}

/* If ‘size’ is smaller than or equal to the current block’s size, reuses the same block.
Otherwise, finds/allocates ‘size’ bytes for a new space, copies content of oldp into the
new allocated space and frees the oldp.
Return value:
i. Success –
a. Returns pointer to the first byte in the (newly) allocated space.
b. If ‘oldp’ is nullptr, allocates space for ‘size’ bytes and returns a pointer to it.
ii. Failure –
a. If size is 0 returns nullptr.
b. If ‘size’ if more than 10^8
, return nullptr.
c. If sbrk fails in allocating the needed space, return nullptr.
d. Do not free ‘oldp’ if srealloc() fails.*/
void* srealloc(void* oldp, size_t size){
    // last wilderness
    if(oldp == nullptr) {
        return smalloc(size);
    }

    size_t new_size = alignSize(size);
    MallocMetadata* meta_ptr = getMallocMetadataFromPointer(oldp);
    assert(meta_ptr != nullptr);
    size_t save_size = meta_ptr->size;

    if(new_size >= 108 * 1024) {
        if(meta_ptr->size == new_size) {
            return (void*)((char*)meta_ptr + sizeof(MallocMetadata)); 
        }
        MallocMetadata* temp = (MallocMetadata*)smalloc(new_size);
        if(temp == nullptr) {
            return nullptr;
        }
        memmove((void*)((char*)temp + sizeof(MallocMetadata)), oldp, save_size);
        sfree(oldp);
        return (void*)((char*)temp + sizeof(MallocMetadata)); 
    }

    //check if there's room in the same block
    // a
    if(meta_ptr->size >= new_size) {
        _split(meta_ptr, new_size);
        return oldp;
    }

    //b
    MallocMetadata* prev_oldp = meta_ptr->prev;
    if((prev_oldp != nullptr) && (prev_oldp->is_free == true) && (prev_oldp->size + meta_ptr->size + sizeof(MallocMetadata) >= new_size)) {
       MallocMetadata* temp = _unionFromLeft(meta_ptr);
       temp->is_free = false;
       _deleteFromFreeList(temp);
       _split(temp, new_size);
       memmove((void*)((char*)temp + sizeof(MallocMetadata)), oldp, save_size);
       return (void*)((char*)temp + sizeof(MallocMetadata)); 
    }

    //b2 + c
    MallocMetadata* wilderness = _getLast();
    if(wilderness == meta_ptr) {
        wilderness = _unionFromLeft(meta_ptr);
        wilderness->is_free = false;
       _deleteFromFreeList(wilderness);
        if(wilderness->size < new_size) {
            MallocMetadata* new_program_break = (MallocMetadata*)sbrk((int)size - wilderness->size);
            if(new_program_break == (void*)(-1)) {
                return nullptr;
            } 
            wilderness->is_free = false;
            wilderness->size = new_size;
            wilderness->free_next = nullptr;
            wilderness->free_prev = nullptr;
        }
        std::memmove((void*)((char*)wilderness + sizeof(MallocMetadata)), oldp, save_size);
        return (void*)((char*)wilderness + sizeof(MallocMetadata));
    }

    // d
    MallocMetadata* next_oldp = meta_ptr->next;
    if((next_oldp != nullptr) && (next_oldp->is_free == true) && (next_oldp->size + meta_ptr->size + sizeof(MallocMetadata) >= new_size)) {
       MallocMetadata* temp = _unionFromRight(meta_ptr);
       temp->is_free = false;
       _deleteFromFreeList(temp);
       _split(temp, new_size);
      // memmove((void*)((char*)temp + sizeof(MallocMetadata)), oldp, save_size);
       return (void*)((char*)temp + sizeof(MallocMetadata));
    }

    // e
    MallocMetadata* new_meta_ptr = _unionFromLeft(meta_ptr);
    if(new_meta_ptr != meta_ptr) {
        new_meta_ptr->is_free = false;
        _deleteFromFreeList(new_meta_ptr);
    }

    if(new_meta_ptr->next->is_free == true) {
        _unionFromRight(new_meta_ptr);
        new_meta_ptr->is_free = false;
        _deleteFromFreeList(new_meta_ptr);
        if(new_meta_ptr->size >= new_size) {
            _split(new_meta_ptr, new_size);
        memmove((void*)((char*)new_meta_ptr + sizeof(MallocMetadata)), oldp, save_size);
        return (void*)((char*)new_meta_ptr + sizeof(MallocMetadata));
        }
    }

    wilderness = _getLast();
    if(wilderness == new_meta_ptr) {
        if(wilderness->size < new_size) {
            MallocMetadata* new_program_break = (MallocMetadata*)sbrk((int)size - wilderness->size);
            if(new_program_break == (void*)(-1)) {
                return nullptr;
            } 
            wilderness->is_free = false;
            wilderness->size = new_size;
            wilderness->free_next = nullptr;
            wilderness->free_prev = nullptr;
        }
        memmove((void*)((char*)wilderness + sizeof(MallocMetadata)), oldp, save_size);
        return (void*)((char*)wilderness + sizeof(MallocMetadata));
    }

    // g
    void* new_block = smalloc(new_size);
    if(new_block == nullptr) {
        return nullptr;
    }  
    memmove(new_block, oldp, save_size);
    sfree(oldp);
    return new_block;
}

/* Returns the number of allocated blocks in the heap that are currently free.*/ 
size_t _num_free_blocks() {
    size_t counter_free = 0;
    MallocMetadata* current_block = free_list;
    while(current_block != nullptr) {
        //if(current_block->is_free == true) {
            counter_free++;
        //}
        current_block = current_block->free_next;
    } 
    return counter_free;
}

/* Returns the number of bytes in all allocated blocks in the heap that are currently free,
excluding the bytes used by the meta-data structs. */
size_t _num_free_bytes() {
    size_t number_of_free_bytes = 0;
    MallocMetadata* current_block = init;
    while(current_block != nullptr) {
        if(current_block->is_free == true) {
            number_of_free_bytes += current_block->size;
        }
        current_block = current_block->next;
    } 
    return number_of_free_bytes;
}

size_t _num_mmapped_blocks() {
    size_t counter = 0;
    MallocMetadata* current_block = mmap_list;
    while(current_block != nullptr) {
        counter++;
        current_block = current_block->next;
    } 
    return counter;
}

/* Returns the overall (free and used) number of allocated blocks in the heap and mmapped. */
size_t _num_allocated_blocks() {
    size_t counter = _num_mmapped_blocks();
    MallocMetadata* current_block = init;
    while(current_block != nullptr) {

        counter++;
        current_block = current_block->next;
    } 
    return counter;
}

size_t _num_mmapped_bytes(){
    size_t number_of_bytes = 0;
    MallocMetadata* current_block = mmap_list;
    while(current_block != nullptr) {
        number_of_bytes += current_block->size;
        current_block = current_block->next;
    } 
    return number_of_bytes;  
}

/* Returns the overall number (free and used) of allocated bytes in the heap and mmapped, excluding
the bytes used by the meta-data structs. */
size_t _num_allocated_bytes() {
    size_t number_of_bytes = _num_mmapped_bytes();
    MallocMetadata* current_block = init;
    while(current_block != nullptr) {
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
//     cout << "======start smalloc test======\n" << endl;
//     void* program_break = sbrk(0);
//     cout << "the initial program break:" << (long int*)program_break << endl;
//     void* ptr1 = smalloc(10);
//     cout << "the ptr to allocated block of 15 bytes (without meta data) is:" << (long int*)ptr1 << endl;
//     assert((char*)ptr1 - (char*)program_break == sizeof(MallocMetadata));
//     program_break = sbrk(0);
//     void* ptr2 = srealloc(ptr1, 100);
//     assert(ptr1 == ptr2);
// }
    // cout << "the program break after allocating 15 bytes is:" << (long int*)program_break << endl;
    // assert((char*)program_break - (char*)ptr1 == 16);
    // void* ptr2 = smalloc(44);
    // cout << "the ptr to allocated block of 44 bytes (without meta data) is:" << (long int*)ptr2 << endl;
    // assert((char*)ptr2 - (char*)program_break == sizeof(MallocMetadata));
    // program_break = sbrk(0);
    // cout << "the program break after allocating 44 bytes is:" << (long int*)program_break << endl;
    // assert((char*)program_break - (char*)ptr2 == 48);
    // void* ptr3 = scalloc(3, 31);
    // cout << "the ptr to allocated block of 3*31 bytes(without meta data) with scalloc is:" << (long int*)ptr3 << endl;
    // assert((char*)ptr3 - (char*)program_break == sizeof(MallocMetadata));
    // program_break = sbrk(0);
    // cout << "the program break after allocating 3*32 bytes is:" << (long int*)program_break << endl;
    // assert((char*)program_break - (char*)ptr3 == 3*32);
//     // cout << "\n======passed simple test======\n" << endl;
//     return;
//  }

//  void test_smalloc_holes(){
//      cout << "======start smalloc test======\n" << endl;
//      void* program_break = sbrk(0);
//      cout << "the initial program break:" << (long int*)program_break << endl;
//      void* ptr1 = smalloc(10);
//      cout << "the ptr to allocated block of 10 bytes (without meta data) is:" << (long int*)ptr1 << endl;
//      assert((char*)ptr1 - (char*)program_break == sizeof(MallocMetadata));
//      program_break = sbrk(0);
//      cout << "the program break after allocating 10 bytes is:" << (long int*)program_break << endl;
//      assert((char*)program_break - (char*)ptr1 == 16);
//      void* ptr2 = smalloc(10);
//      cout << "the ptr to allocated block of 10 bytes (without meta data) is:" << (long int*)ptr2 << endl;
//      assert((char*)ptr2 - (char*)program_break == sizeof(MallocMetadata));
//      program_break = sbrk(0);
//      cout << "the program break after allocating 10 bytes is:" << (long int*)program_break << endl;
//      assert((char*)program_break - (char*)ptr2 == 16);
//      void* ptr3 = smalloc(10);
//      cout << "the ptr to allocated block of 10 bytes (without meta data) is:" << (long int*)ptr3 << endl;
//      assert((char*)ptr3 - (char*)program_break == sizeof(MallocMetadata));
//      program_break = sbrk(0);
//      cout << "the program break after allocating 10 bytes is:" << (long int*)program_break << endl;
//      assert((char*)program_break - (char*)ptr3 == 16);
//      void* ptr4 = smalloc(10);
//      cout << "the ptr to allocated block of 10 bytes (without meta data) is:" << (long int*)ptr4 << endl;
//      assert((char*)ptr4 - (char*)program_break == sizeof(MallocMetadata));
//      program_break = sbrk(0);
//      cout << "the program break after allocating 10 bytes is:" << (long int*)program_break << endl;
//      assert((char*)program_break - (char*)ptr4 == 16);
//      void* ptr5 = smalloc(10);
//      cout << "the ptr to allocated block of 10 bytes (without meta data) is:" << (long int*)ptr5 << endl;
//      assert((char*)ptr5 - (char*)program_break == sizeof(MallocMetadata));
//      program_break = sbrk(0);
//      cout << "the program break after allocating 10 bytes is:" << (long int*)program_break << endl;
//      assert((char*)program_break - (char*)ptr5 == 16);
//      sfree(ptr1);
//      sfree(ptr3);
//      sfree(ptr5);
//      ptr5 = smalloc(10);
//      cout << "the ptr to allocated block of 10 bytes (without meta data) is:" << (long int*)ptr5 << endl;
//      assert((char*)ptr5 - (char*)program_break == sizeof(MallocMetadata));
//      program_break = sbrk(0);
//      cout << "the program break after allocating 10 bytes is:" << (long int*)program_break << endl;
//      assert((char*)program_break - (char*)ptr5 == 10);
//      cout << "\n======passed simple test======\n" << endl;
//      return;
//  }

// void test_smalloc_with_mmap(){
//     cout << "======start smalloc with mmap test======\n" << endl;
//     void* ptr1 = smalloc(128*1024);
//     cout << "the ptr to allocated block of 128*1024 bytes (without meta data) is:" << (long int*)ptr1 << endl;
//     void* ptr2 = smalloc(136*1024);
//     cout << "the ptr to allocated block of 136*1024 bytes (without meta data) is:" << (long int*)ptr2 << endl;
//     void* ptr3 = scalloc(136,1024);
//     cout << "the ptr to allocated block of 136*1024 bytes(without meta data) with scalloc is:" << (long int*)ptr3 << endl;
//     void* program_break = sbrk(0);
//     cout << "the initial program break:" << (long int*)program_break << endl;
//     void* ptr4 = smalloc(8);
//     cout << "the ptr to allocated block of 184 bytes (without meta data) is:" << (long int*)ptr4 << endl;
//     assert((char*)ptr4 - (char*)program_break == sizeof(MallocMetadata));
//     program_break = sbrk(0);
//     cout << "the program break after allocating 184 bytes is:" << (long int*)program_break << endl;
//     assert((char*)program_break - (char*)ptr4 == 8);
//     sfree(ptr2);
//     sfree(ptr3);
//     sfree(ptr1);
//     sfree(ptr4);
//     cout << "\n======passed smalloc with mmap test======\n" << endl;
//     return;
// }

// void test_simple_split(){
//     cout << "======start simple split test======\n" << endl;
//     void* program_break = sbrk(0);
//     cout << "the initial program break:" << (long int*)program_break << endl;
//     void* ptr1 = smalloc(184);
//     cout << "the ptr to allocated block of 184 bytes (without meta data) is:" << (long int*)ptr1 << endl;
//     assert((char*)ptr1 - (char*)program_break == sizeof(MallocMetadata));
//     program_break = sbrk(0);
//     cout << "the program break after allocating 184 bytes is:" << (long int*)program_break << endl;
//     assert((char*)program_break - (char*)ptr1 == 184);
//     void* ptr2 = smalloc(160);
//     cout << "the ptr to allocated block of 160 bytes (without meta data) is:" << (long int*)ptr2 << endl;
//     assert((char*)ptr2 - (char*)program_break == sizeof(MallocMetadata));
//     program_break = sbrk(0);
//     cout << "the program break after allocating 160 bytes is:" << (long int*)program_break << endl;
//     assert((char*)program_break - (char*)ptr2 == 160);
//     void * ptr3 = smalloc(144);
//     cout << "the ptr to allocated block of 144 bytes (without meta data) is:" << (long int*)ptr3 << endl;
//     assert((char*)ptr3 - (char*)program_break == sizeof(MallocMetadata));
//     program_break = sbrk(0);
//     cout << "the program break after allocating 144 bytes is:" << (long int*)program_break << endl;
//     assert((char*)program_break - (char*)ptr3 == 144);
//     sfree(ptr1);
//     sfree(ptr3);
//     sfree(ptr2);
//     ptr2 = smalloc(8);
//     assert(ptr1 == ptr2);
//     cout << "\n======passed simple split test======\n" << endl;
//     return;
// }

// void test_error(){
//     cout << "======start error test======\n" << endl;
//     void* program_break = smalloc(0);
//     assert(program_break==nullptr);
//     program_break = smalloc(pow(10,8)+1);
//     assert(program_break==nullptr);
//     program_break = scalloc(0,10);
//     assert(program_break==nullptr);
//     program_break = scalloc(10,0);
//     assert(program_break==nullptr);
//     program_break = scalloc(1,pow(10,8)+1);
//     assert(program_break==nullptr);
//     program_break = srealloc(program_break,pow(10,8)+1);
//     assert(program_break==nullptr);
//     program_break = srealloc(program_break,0);
//     assert(program_break==nullptr);
// //     cout << "\n======passed error test======\n" << endl;
// //     return;
// // })


// template <typename T>
// void populate_array(T *array, size_t len)
// {
//     for (size_t i = 0; i < len; i++)
//     {
//         array[i] = (T)i;
//     }
// }

// template <typename T>
// void validate_array(T *array, size_t len)
// {
//     for (size_t i = 0; i < len; i++)
//     {
//         assert((array[i] == (T)i));
//     }
// }

// void test() {
 

//     char *a = (char *)smalloc(128 + 32);
//     char *b = (char *)smalloc(32);
//     char *c = (char *)smalloc(32);
    


//     sfree(a);
//     sfree(c);


//     char *new_b = (char *)srealloc(b, 64);


    
// }



//   int main(){
//       //test_error();
//      //test_simple_split();
//    //   test_smalloc_holes();
//    //  test_srealloc_simple();
//      test();
//       //test_smalloc_with_mmap();
//       return 0;
//   }