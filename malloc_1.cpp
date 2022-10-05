#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

/* Tries to allocate ‘size’ bytes.
Return value:
i. Success: a pointer to the first allocated byte within the allocated block.
ii. Failure:
    a. If ‘size’ is 0 returns NULL.
    b. If ‘size’ is more than 10^8, return NULL.
    c. If sbrk fails, return NULL.*/ 
void* smalloc(size_t size){
    if( size == 0 || size > pow(10,8)){
        return NULL;
    }
    void* program_break = sbrk(sizeof(char)*size);
    if(program_break == (void*)(-1)){
        return NULL;
    }
    return program_break;
}

int counter = 0;

// int main(){
//     printf("the initial counter: %d\n",counter);
//     counter++;
//     printf("counter plus one: %d\n",counter);

//     void* program_break = sbrk(0);
//     printf("the initial program break: %p\n",(long int*)program_break);

//     program_break = smalloc(0);
//     program_break = smalloc(pow(10,8)+1);
//     program_break = smalloc(10);
//     printf("the program break before allocating 10 bytes is: %p\n",(long int*)program_break);
//     program_break = smalloc(20);
//     printf("the program break before allocating 20 bytes is: %p\n",(long int*)program_break);
//     program_break = smalloc(30);
//     printf("the program break before allocating 30 bytes is: %p\n",(long int*)program_break);
//     program_break = sbrk(0);
//     printf("the program break after allocating 30 bytes is: %p\n",(long int*)program_break);
//     return 0;
// }