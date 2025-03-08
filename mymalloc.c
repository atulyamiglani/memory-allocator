#define _DEFAULT_SOURCE
#define _BSD_SOURCE 
#include <malloc.h> 
#include <stdio.h> 
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <math.h>
#include <sys/mman.h>
#include <pthread.h>

// Include any other headers we need here
// NOTE: You should NOT include <stdlib.h> in your final implementation

#include <debug.h> 
#define BLOCK_SIZE sizeof(block_t)
#define PAGE_SIZE sysconf(_SC_PAGE_SIZE) 


pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// BLOCK HEADER - for each block of memory 
typedef struct block {
  int size;
  struct block *next;
  int free;
  
} block_t;

block_t head = {0, NULL, 0};


//Returns the previous block.
block_t *prev_block(block_t *b) {
  block_t *current = &head;
  while (current->next != b) {
    current = current->next;
  }

  return current; 
}

//Implements first fit method by finding the first free block in the linked list
block_t *next_free(block_t *head, size_t size) {
	block_t* current = head;
	while (current) {
		if  (current && current->free==1  &&  current->size >= size) {
			debug_printf("Found free block of existing size %zu \n", current->size);
			current->free = 0;
			return current;			
		}	
			
		else {
			current = current->next; 
		}

	}
	assert(!current); 
	return current;

}

//Returns the tail of the list
block_t* get_tail(block_t* head) {
	block_t* curr = head; 
	while(curr && curr->next != NULL) {
		curr = curr->next; 
	} 
	return curr; 
}


//Creates new pages of memory with mmap
void *make_new_pages(int pages) {
  void *new = mmap(NULL, pages * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

  block_t *last = get_tail(&head);
  last->next = new;
  last->next->free = 0;
  last->next->size = pages * PAGE_SIZE - BLOCK_SIZE;
  last->next->next = NULL;
  pthread_mutex_unlock(&mutex);
  return (void *) new + BLOCK_SIZE;
}


// splits the block of memory into two blocks and makes it the last element of the list 
void *divide_memory(size_t s) {
  void *new = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

  block_t *last = get_tail(&head);
  block_t *last_next = last->next;

  last_next = new;
  last_next->free = 0;
  last_next->size = s;
  
  block_t *new_split = ((void *) new) + s + BLOCK_SIZE;
  last_next->next = new_split;
  new_split->free = 1;
  new_split->next = NULL;
  new_split->size = PAGE_SIZE - BLOCK_SIZE - BLOCK_SIZE - s;
  
  pthread_mutex_unlock(&mutex);
  return (void *) new + BLOCK_SIZE;
}



//Splits a block 
void *split_block(block_t *block, size_t s) {  
   
  int block_size = block->size;
  block_t* block_next = block->next;
  block_t *split_start = ((void *) block) + BLOCK_SIZE + s;
  split_start->free = 1;
  split_start->next = block_next;
  split_start->size = block_size - BLOCK_SIZE - s;

  block->free = 0;
  block->next = split_start;
  block->size = s;  
  pthread_mutex_unlock(&mutex);
  return ((void *) block) + BLOCK_SIZE;
}

// calculates the number of pages from the given size 
int get_pages(size_t s) {
	  int num_pages = (s + BLOCK_SIZE) / PAGE_SIZE;
 	 // round the pages to the nearest page 
	if ((s + BLOCK_SIZE) % PAGE_SIZE > 0) {
       num_pages++;
  	}
	return num_pages; 
}

//Malloc function to work as a custom memory allocator
void *mymalloc(size_t s) {
  pthread_mutex_lock(&mutex);
  debug_printf("Malloc %zu bytest", s);

 	int pages = get_pages(s); 
 
  if (s + BLOCK_SIZE >= PAGE_SIZE) {
    return make_new_pages(pages);
  } 
  else {
	block_t *found = next_free(&head, s);

 	if (found == NULL) {
    
    	if (PAGE_SIZE - BLOCK_SIZE - s <= BLOCK_SIZE) {
      		return make_new_pages(1);
    	} else {
      		return divide_memory(s);
    	}
  	} else {
    
    if ((int) (found->size - s - BLOCK_SIZE) >=  1) {
      return split_block(found, s);
    } else {
      found->free = 0;
      pthread_mutex_unlock(&mutex);
      return ((void *) found) + BLOCK_SIZE;
    }
  }
 }
}

//Calloc memory
void *mycalloc(size_t nmemb, size_t s) {
  debug_printf("calloc %zu bytes\n", nmemb * s);

  void *p = mymalloc(nmemb * s);
  memset(p, 0, nmemb * s);
  return p;
}

//Coalesce blocks
void coalesce() {
  block_t *current = &head;

  while (current->next != NULL) {
    block_t *next = current->next;
		// colease blocks if they are free and adjacent 
    if (next->free == 1  && current->free == 1 && current->next == (void *) current + BLOCK_SIZE + current->size) {
      current->next = next->next;
      current->size += next->size + BLOCK_SIZE;
      
    } else {
      current = current->next;
    }
  }
}

//Frees memory
void myfree(void *ptr) {
  pthread_mutex_lock(&mutex);
  block_t *block = ptr - BLOCK_SIZE;
  // free and coalesce if block is less than a page size 
  if (block->size + BLOCK_SIZE < PAGE_SIZE) {
    block->free = 1;
    debug_printf("Freed block of size %zu", block->size);
    coalesce();

  } else {
		// unmap memory for page sizes greater than page sizes 
    int size = block->size;
    block_t *prev = prev_block(block);
    prev->next = block->next;
    munmap((void *) block, size + BLOCK_SIZE);
    debug_printf("Unmapped %zu bytes \n", size + BLOCK_SIZE);
  }
  pthread_mutex_unlock(&mutex);
}


