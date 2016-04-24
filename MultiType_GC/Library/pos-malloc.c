/*
   Persistent Object Store
   
   Author: Taeho Hwang (htaeh@hanyang.ac.kr)
   Embedded Software Systems Laboratory, Hanyang University
*/


/* Malloc implementation for multiple threads without lock contention.
   Copyright (C) 1996-2006, 2007, 2008, 2009 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Wolfram Gloger <wg@malloc.de>
   and Doug Lea <dl@cs.oswego.edu>, 2001.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License as
   published by the Free Software Foundation; either version 2.1 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/*
  160311
  1. allocation tree insertion routine added.
     => address(nv object chunk's start address) will be inserted to allocation tree(rb-tree) 
 */

/*160315
  1. previous allocation tree insertion routine deleted
  2. rb-tree insert/search function added
  3. gc_node structure added
  4. rb-tree initialization routine added
*/

#include <pos-malloc.h>
#include <stdio.h>
#include <unistd.h>
#include <syscall.h>

//sb s
#include "KV/alloc_list/alloc_list.h"
#include "KV/list/pos-list.h"
#include "KV/hashtable/pos-hashtable.h"
#include "KV/btree/pos-btree.h"
//sb e

INTERNAL_SIZE_T global_max_fast = 144;

//dk start
#define POS_DEBUG 1
#define POS_DEBUG_MALLOC	1
//dk end

#define pos_public_fREe		pos_free
#define pos_public_mALLOc	pos_malloc
#define pos_public_rEALLOc	pos_realloc

unsigned long total_chunks_size=0;
int total_idx = 0;
int seg_alloc_count = 0;
int new_alloc_count = 0;
unsigned long garbage_count = 0;

static void pos_malloc_consolidate(char *,mstate);
static Void_t* pos_int_malloc(char *, mstate, size_t);
void pos_int_free(char *, mstate, mchunkptr, int);
void pos_malloc_init_state(char *, mstate);
Void_t* pos_public_mALLOc(char *, unsigned long);
void pos_public_fREe(char *, Void_t *);
Void_t* pos_public_rEALLOc(char *, Void_t*, unsigned long);

void pos_print_free_chunks(char *);

void pos_print_seg(mstate av);

Void_t* lookup_pointer(mchunkptr p , unsigned long offset);
int check_valid_address(struct seg_info *head, Void_t *ptr);
void pos_check_unsafe_segment(char *name, mstate av, struct seg_info *head, Void_t *first_chunk);
Void_t* pos_unsafe_region_relocate(char *name, mstate av, Void_t *p);
void pos_check_unsafe_region(char *name, mstate av, struct seg_info *head, mchunkptr chunk_ptr);
void chunk_change_pointer(mchunkptr chunk_ptr, Void_t *p , unsigned long offset);
/*
//DK start
int pos_gc_node_insert(struct rb_root *root, GC_NODE *key_node)
{
  struct rb_node **new_node = &(root->rb_node);
  struct rb_node *parent = NULL;
  GC_NODE *current_node;
  int determine = 0;

  while(*new_node)
  {
      current_node = rb_entry(*new_node, GC_NODE, node);
      determine = key_node->key - this_node->key;
      parent = *new_node;
      if(determine < 0)
      {
		  new_node = &((*new)->rb_left);
      }
      else if(determine > 0)
      {
		  new_node = &((*new)->rb_right);
      }
      else
      {
		  return 0;
      }
      rb_link_node(&key_node->node, parent, new);
      rb_insert_color(&data->node, root);

      return 1;
}
//DK end

//DK start
GC_NODE pos_gc_node_search(struct rb_root *root, unsigned long key);
{
  struct rb_node *node_pointer; 
  GC_NODE *key_node;
  int determine = 0;

  node = root->rb_node;
  while(node_pointer)
  {
    key_node = rb_entry(node_pointer, GC_NODE, node);
    determine = key - key_node->key;
    if(determine < 0)
    {
      node_pointer = node->rb_left;
    }
    else if(determine > 0)
    {
      node_pointer = node->rb_right;
    }
    else
    {
      return key_node;
    }
  }
  return NULL;
}
//DK end

GC_NODE create_gc_node(GC_NODE *node)
{
	GC_NODE* new_node = pos_malloc("gc_tree", sizeof(GC_NODE), 2);
	rb_init_node(&new_node->node);
	new_node->key = 0;
}
*/

//dk s
int pos_local_gc(char* name)
{
	mchunkptr ptr, next_chunk, next_seg_ptr;
	void *mem_ptr;
	void *p;
	short obj_type = 0;
	int type;
	int size;
	int key_count;
	int val_count;
	Node *alloc_list_head = NULL;
	Node *cur_node = NULL;
	mstate ms_ptr;
	unsigned int list_state=0;

	ms_ptr = (struct malloc_state *)pos_lookup_mstate(name);  
	p = pos_get_prime_object(name);
	ptr = mem2chunk(p);

#if POS_DEBUG_MALLOC == 1
	printf("[local gc] allocation list status before gc\n");
	display(alloc_list_head);
	printf("\n");	
#endif

//#if POS_DEBUG_MALLOC == 1
	//printf("[local gc] pos list status before gc\n");
	//display(alloc_list_head);
	//print_list(name);
	//printf("\n");	
//#endif

#if POS_DEBUG_MALLOC == 1
	printf("[local gc] GC start!\n");
#endif

//if(POS_DEBUG_MALLOC == 1)
//{
//printf("[gc] 1\n");
//}
	//obj_type = pos_get_object_type(name);
	syscall(308, name, &obj_type);

	type = obj_type & 0xF; //1111
	size = obj_type & 0xF0; //11110000
	key_count = obj_type & 0xF00; //111100000000
	val_count = obj_type & 0XF000; //1111000000000000

#if POS_DEBUG_MALLOC == 1
	printf("[local gc] type : %d\n", type);
#endif

	val_count = obj_type & 0XF000; //1111000000000000	
if(POS_DEBUG_MALLOC == 1)
{
printf("[gc] 2\n");
}
    //dk s
    alloc_list_head = NULL;
    //dk e

	if(type == 1) // linked list
	{
		//dk s
		int list_ret = 0; //for debugging
		list_ret = make_list_for_list((struct list_head *)p, &alloc_list_head);
		if(list_ret == -1)
		{
			printf("make_list_for_list ERROR\n");
			return;
		}
		//dk e 
		//alloc_list_head = (Node *) get_alloc_head();
	}
	else if(type == 2) //b-tree
	{
		//sb s
	  //return;
		int btree_ret = 0;
		btree_ret = make_list_for_btree((struct btree_head *)p, &alloc_list_head);
		if(btree_ret == -1)
		{
			printf("make_list_for_btree ERRO\n");
		}
		//sb e
	}
	else if(type == 3) //hash
	{
		int hash_ret = 0;
		hash_ret = make_list_for_hashtable((struct hashtable *)p, &alloc_list_head);
		if(hash_ret == -1)
		{
			printf("make_list_for_hash ERROR\n");
			return;
		}
	}
	else
	{
#if POS_DEBUG_MALLOC == 1
		printf("[local gc] wrong storage type!\n");
#endif
		return -1;
	}
#if POS_DEBUG_MALLOC == 1
	printf("[local gc] allocation list status\n");
	display(alloc_list_head);
	printf("\n");
	//printf("[local gc] pos list status\n");
	//print_list(name);
	//printf("\n");	
#endif

	if(alloc_list_head == NULL) 
    {
		printf("[local gc] allocation list is NULL!\n");
		return -1;
	}
	else 
	{
#if POS_DEBUG_MALLOC == 1
	printf("[local gc] list node size : %lu\n", sizeof(struct list_node));
	printf("[local gc] value * 2 : %lu\n", sizeof(unsigned long)*2);
#endif
	}

#if POS_DEBUG_MALLOC == 1
printf("[gc] 3\n");
#endif
	cur_node = alloc_list_head;
	while(ptr != ms_ptr->last_chunk_pointer)
	{
#if POS_DEBUG_MALLOC == 1
		printf("==================================================\n");
		printf("[local gc] chunk addr : %p\n", ptr);
		printf("[local gc] chunk size : %lu\n", chunksize(ptr));
		if(cur_node != NULL)
			printf("[local gc] cur_node->addr : %p\n", (void *)cur_node->addr);
		printf("chk\n");
#endif
		//total_chunks_size += chunksize(ptr);
//		while(!inuse(ptr))
//		{
//			printf("free chunk!\n");
//			ptr = next_chunk(ptr);
//		}
		mem_ptr = chunk2mem(ptr);
		printf("[local gc] mem_ptr : %p\n", mem_ptr);
		
		while(!inuse(ptr)) //if next chunk is free, pass free chunks
		{
#if POS_DEBUG_MALLOC == 1
			printf("[local gc] 0\n");
#endif
			ptr = next_chunk(ptr);
			if(chunk_is_last(ptr) == 0x4)
			{
				printf("jump from passing free chunk to next segment\n");
				next_seg_ptr = next_seg(ptr, chunksize(ptr));
				ptr = (mchunkptr)(chunksize(next_seg_ptr));
			}
		 }
		printf("[local gc] chunk addr : %p\n", ptr);
		printf("[local gc] chunk size : %lu\n", chunksize(ptr));

		if((void *)cur_node->addr == mem_ptr)
		{
#if POS_DEBUG_MALLOC == 1
			printf("[local gc] 1\n");
			printf("[local gc] cur_node->addr : %p\n", (void *)cur_node->addr);
#endif
			cur_node = cur_node->next;
			ptr = next_chunk(ptr);
			printf("is last? : %d\n", (int)chunk_is_last(ptr));

			//sb s
			//list_state = get_list_state();
			switch(type)
			{
				case 1 : list_state = get_list_state(); printf("list state!\n"); break;
				case 2 : list_state = get_btree_state(); printf("btree state!\n"); break;
				case 3 : list_state = get_hash_state(); printf("hashtable state!\n"); break;
				default : printf("[local gc] wrong type!\n"); return;
			}
			//sb e
			next_chunk = next_chunk(ptr);
			printf("list_state : %d\n", list_state);
			if(list_state == 1 && chunk_is_last(next_chunk) == 0x4) 
			{
				//next_seg_ptr = next_seg(ms_ptr->last_chunk_pointer, chunksize(ms_ptr->last_chunk_pointer));
				//printf("last chunk p : %p, next_chunk : %p\n", ms_ptr->last_chunk_pointer, next_chunk);
				next_seg_ptr = next_seg(next_chunk, chunksize(next_chunk));
				printf("next_seg_ptr : %p\n", (void *)chunksize(next_seg_ptr));

				if(chunksize(next_seg_ptr) != 0) //there is next seg, so there is no dangling chunk
				{
					mem_ptr = chunk2mem(ptr);
										
					printf("mem_ptr : %p, next_seg_ptr : %p\n", mem_ptr, (void *)chunksize(next_seg_ptr));
					if((void *)cur_node->addr == mem_ptr) //last-1 chunk is not garbage
					{
						if(inuse(next_chunk)) //if last chunk is in use
						{
							unsigned long *l_tmp = chunk2mem(next_chunk); //ptr of last chunk
							printf("in inuse(next_chunk) l_tmp : %p, cur_node->next->addr : %p\n", l_tmp, (void *)cur_node->next->addr);
							if(l_tmp == (void *)cur_node->next->addr) //last chunk is not garbage
							{
								printf("in inuse(next_chunk) and same\n");
								cur_node = cur_node->next->next;
								//dk s
								ptr = (mchunkptr)(chunksize(next_seg_ptr));
								//dk e
							}
							else //lastchunk == garbage
							{ 
								pos_free(name, l_tmp);
								//dk s
								printf("[local gc] list_state = %d, last-1 = not garbage, last = garbage\n", list_state);
								printf("[local gc] ptr(%p) is garbage -> freed!\n", l_tmp);
								garbage_count++;
								printf("[local gc] garbage count : %lu\n", garbage_count);
								cur_node = cur_node->next;
								ptr = (mchunkptr)(chunksize(next_seg_ptr));
								//dk e
							}
							//dk s (delete some code)
							//dk e
						}
						else //if last chunk is free
						{
							printf("last chunk is a free chunk\n");
							ptr = (mchunkptr)(chunksize(next_seg_ptr));
						}
						//dk s
						//printf("next_seg_ptr = %p\n", next_seg_ptr);
						//ptr = (mchunkptr)(chunksize(next_seg_ptr));
						//printf("ptr : %p\n", ptr);
						//cur_node=cur_node->next;
						//printf("ptr : %p\n", chunk2mem(ptr));
						//printf("cur_node->next->addr : %p\n", (void *)cur_node->addr);
						//printf("last-1 chunk is not a garbage\n");
						//dk e
					}	
					else //last-1 chunk is garbage
					{
						pos_free(name, mem_ptr);
						printf("[local gc] list_state = %d, last chunk-1 = garbage\n", list_state);
						printf("[local gc] ptr(%p) is garbage -> freed!\n", mem_ptr);
						garbage_count++;
						printf("[local gc] garbage count : %lu\n", garbage_count);

						if(inuse(next_chunk)) //if last chunk is in use
						{
							unsigned long *l_tmp = chunk2mem(next_chunk); //ptr of last chunk
							printf("in inuse(next_chunk) l_tmp : %p, cur_node->addr : %p\n", l_tmp, (void *)cur_node->next->addr);
							if(l_tmp == (void *)cur_node->next->addr) //last chunk is not garbage
							{
								printf("in inuse(next_chunk) and same\n");
								cur_node = cur_node->next->next;
								//dk s
								ptr = (mchunkptr)(chunksize(next_seg_ptr));
								//dk e
							}
							else //lastchunk == garbage
							{ 
								//dk s
								printf("[local gc] list_state = %d, last-1 = garbage, last = garbage\n", list_state);
								pos_free(name, l_tmp);
								printf("[local gc] ptr(%p) is garbage -> freed!\n", l_tmp);
								garbage_count++;
								printf("[local gc] garbage count : %lu\n", garbage_count);
								ptr = (mchunkptr)(chunksize(next_seg_ptr));
								//dk e
							}			
						}
						else //if last chunk is free
						{
							printf("last chunk is a free chunk/n");
							ptr = (mchunkptr)(chunksize(next_seg_ptr));
						}
					}
				}
				else //there is no next seg and there is dangling chunk
				{
					printf("in list state 1-1\n");
					printf("there is no next seg\n");
					break;
				}
			}
			else if(list_state == 2 && chunk_is_last(ptr) == 0x4) 
			{
				printf("in list state 2-0\n");
				//next_seg_ptr = next_seg(ms_ptr->last_chunk_pointer, chunksize(ms_ptr->last_chunk_pointer));
				//printf("last chunk p : %p, next_chunk : %p\n", ms_ptr->last_chunk_pointer, next_chunk);
				next_seg_ptr = next_seg(ptr, chunksize(ptr));
				//printf("next_seg_ptr : %p\n", (void *)chunksize(next_seg_ptr));
				
				//dk s
				mem_ptr = chunk2mem(ptr);
										
				printf("mem_ptr : %p, next_seg_ptr : %p\n", mem_ptr, (void *)chunksize(next_seg_ptr));
				
				if(chunksize(next_seg_ptr) != 0) //there is next seg
				{
					if(inuse(ptr)) //if last chunk is in-use...
					{
						if((void *)cur_node->addr == mem_ptr) //last chunk is not garbage 
						{
							printf("next_seg_ptr = %p\n", next_seg_ptr);
							ptr = (mchunkptr)(chunksize(next_seg_ptr));
							printf("ptr : %p\n", ptr);
							cur_node = cur_node->next;
						}
						else //last chunk is garbage
						{
							printf("[local gc] list_state = %d, last = garbage\n", list_state);
							pos_free(name, mem_ptr);
							printf("[local gc] ptr(%p) is garbage -> freed!\n", mem_ptr);
							garbage_count++;
							printf("[local gc] garbage count : %lu\n", garbage_count);
							ptr = (mchunkptr)(chunksize(next_seg_ptr));
						}
					}
					else //last chunk is free chunk
					{
						printf("last chunk is a free chunk\n");
						ptr = (mchunkptr)(chunksize(next_seg_ptr));
					}
				}
				else //there is no next seg
				{
					printf("in list state 2-1\n");
					printf("there is no next seg\n");
					break;
				}
				//dk e
			}
		}
		else
		{
#if POS_DEBUG_MALLOC == 1
			printf("[local gc] 3\n");
#endif
			mem_ptr = chunk2mem(ptr);
			printf("[local gc] cur_node->addr : %p\n", (void *)cur_node->addr);
			printf("[local gc] mem_ptr : %p\n", mem_ptr);
			pos_free(name, mem_ptr);
			printf("[local gc] ptr(%p) is garbage -> freed!\n", mem_ptr);
			garbage_count++;
			printf("[local gc] garbage count : %lu\n", garbage_count);
			ptr = next_chunk(ptr);
		}
		//dk s
		if(chunk_is_last(ptr) == 0x4) //all chunks in this segment are garbage
		{
#if POS_DEBUG_MALLOC == 1
			printf("[local gc] 4\n");
#endif
			mem_ptr = chunk2mem(ptr);
			next_seg_ptr = next_seg(ptr, chunksize(ptr));

			if(chunksize(next_seg_ptr) != 0) // there is a next seg
			{
				if((void *)cur_node->addr == mem_ptr) //Last chunk is not a garbage
				{
					printf("next_seg_ptr = %p\n", next_seg_ptr);
					ptr = (mchunkptr)(chunksize(next_seg_ptr));
					printf("ptr : %p\n", ptr);
					cur_node = cur_node->next;
				}
				else //last chunk is a garbage
				{
					printf("[local gc] cur_node->addr : %p\n", (void *)cur_node->addr);
					printf("[local gc] mem_ptr : %p\n", mem_ptr);
					pos_free(name, mem_ptr);
					printf("[local gc] ptr(%p) is garbage -> freed!\n", mem_ptr);
					garbage_count++;
					printf("[local gc] garbage count : %lu\n", garbage_count);
					ptr = (mchunkptr)(chunksize(next_seg_ptr));					
				}
			}
			else //there is no next seg
			{
				printf("Just last chunk\n");
				break;
			}
		}
		//dk e
	}
	printf("before remove\n");
	remove_list(alloc_list_head);
	alloc_list_head = NULL;	
	printf("after remove\n");

#if POS_DEBUG_MALLOC == 1
	printf("[local gc] GC end!\n");
#endif
if(POS_DEBUG_MALLOC == 1)
{
printf("[gc] 4\n");
}

	printf("[local gc] PRINT ALLOC LIST AGAIN]\n");
	//sb s
	alloc_list_head = NULL;
	switch(type)
	{
		case 1 : make_list_for_list((struct list_head *)p, &alloc_list_head); break;
		case 2 : make_list_for_btree((struct btree_head *)p, &alloc_list_head); break;
		case 3 : make_list_for_hashtable((struct hashtable *)p, &alloc_list_head); break;
	}
	display(alloc_list_head);
	
	printf("[local gc] REMOVE ALLOC LIST AND FINISH GC\n");
	remove_list(alloc_list_head);
	//sb e

	return 0;
}
//dk e

/*
  ------------------------------ pos_malloc_consolidate ------------------------------
*/

static void 
pos_malloc_consolidate(char *name, mstate av)
{
	mfastbinptr*    fb;
	mfastbinptr*    maxfb;
	mchunkptr       p;
	mchunkptr       nextp;

#if CONSISTENCY == 1
	clear_fastchunks_log(name, av);
#else
	clear_fastchunks(av);
#endif

	maxfb = &fastbin (av, NFASTBINS - 1);

	fb = &fastbin (av, 0);
	do {
		p = *fb;
		if (p != 0) {
#if CONSISTENCY == 1
			POS_WRITE_VAUE(name, (unsigned long *)fb, (unsigned long)0);
#else
			*fb = 0;
#endif
			do {
				nextp = p->fd;
#if CONSISTENCY == 1
				clear_inuse_log(name, p);
#else
				clear_inuse(p);
#endif
				pos_int_free(name, av, p, 0);
//sb s
	printf("			p(%p) pos freed freed\n", p);
//sb e
			} while ( (p = nextp) != 0);
		}
	} while (fb++ != maxfb);

}


/*
  ------------------------------ pos_malloc ------------------------------
*/


static Void_t*
pos_int_malloc(char *name, mstate av, size_t bytes)
{
	INTERNAL_SIZE_T nb;
	unsigned int idx;
	mbinptr bin;

	mchunkptr victim;
	INTERNAL_SIZE_T size;
	int victim_index;

	mchunkptr remainder;
	unsigned long remainder_size;

	unsigned int block;
	unsigned int bit;
	unsigned int map;

	mchunkptr fwd;
	mchunkptr bck;

	//dk s
	int gc_result = 0;
	mchunkptr present_last_chunk = NULL;
	//dk e

	//const char *errstr = NULL;

	size_t pagemask  = PAGESIZE - 1;


 first:
	//16바이트 단위로 정렬
	checked_request2size(bytes, nb);


	// 1. fast bin (<=144)
	if ((unsigned long)(nb) <= (unsigned long)(get_max_fast())) {
		idx = fastbin_index(nb);
		mfastbinptr* fb = &fastbin(av, idx);

		victim = *fb;
if(POS_DEBUG_MALLOC == 1)
{
	printf("fast bin - size : %lu\n", (unsigned long)bytes);
}
		if (victim != 0) {

		/*if (fastbin_index (chunksize (victim)) != idx) {
			errstr = "malloc(): memory corruption (fast)";
errout:
			malloc_printerr (check_action, errstr, chunk2mem (victim));
		}*/

#if CONSISTENCY == 1
			POS_WRITE_VAUE(name, (unsigned long *)fb, (unsigned long)victim->fd);
#else
			*fb = victim->fd;
#endif
			void *p = chunk2mem(victim);

			return p;
		}
	}

	// 2. small bin (<=1008)
if(POS_DEBUG_MALLOC == 1)
{
	printf("[small bin] - size : %lu\n", (unsigned long)bytes);
	printf("[small bin] - current total chunk : %lu\n", total_chunks_size);
	printf("[small bin] nb size : %lu\n", nb);
}
	if (in_smallbin_range(nb)) {
		idx = smallbin_index(nb);
		bin = bin_at(av,idx);


if(POS_DEBUG_MALLOC == 1)
{
	printf("small bin - size : %lu\n", (unsigned long)bytes);
}
		if ( (victim = last(bin)) != bin) {
			bck = victim->bk;

			/*if (bck->fd != victim) {
				errstr = "malloc(): smallbin double linked list corrupted";
				goto errout;
			}*/

#if CONSISTENCY == 1
			set_inuse_bit_at_offset_log(name, victim, nb);
			POS_WRITE_VAUE(name, (unsigned long *)&bin->bk, (unsigned long)bck);
			POS_WRITE_VAUE(name, (unsigned long *)&bin->fd, (unsigned long)bin);
#else
			//dk s
			//set_inuse_bit_at_offset(victim, nb);
			printf("victim : %p, nb : %lu, victim chunk size : %lu\n", victim, nb, chunksize(victim));
			set_inuse_bit_at_offset(victim, chunksize(victim));
			printf("next chunk size : %lu\n", chunksize(next_chunk(victim)));
			//dk e
			bin->bk = bck;
			bck->fd = bin;
#endif
			void *p = chunk2mem(victim);

			return p;
		}
	}
	else {
//sb s
	printf("fastbins consolidate!\n");
//sb e
		idx = largebin_index(nb);

		if (have_fastchunks(av)) {
			pos_malloc_consolidate(name, av);
		}
	}

	for(;;) {

		int iters = 0;

		// 3. unsorted bin
if(POS_DEBUG_MALLOC == 1)
{
	printf("unsorted bin - size : %lu\n", (unsigned long)bytes);
}
		while ((victim = unsorted_chunks(av)->bk) != unsorted_chunks(av)) {
			bck = victim->bk;
			/*if (victim->size <= 2 * SIZE_SZ || victim->size > av->system_mem)
				malloc_printerr (check_action, "malloc(): memory corruption", chunk2mem (victim));*/
			size = chunksize(victim);

			if (in_smallbin_range(nb) &&
			   bck == unsorted_chunks(av) &&
			   victim == av->last_remainder &&
				(unsigned long)(size) > (unsigned long)(nb + MINSIZE)) { //initial state 

				remainder_size = size - nb;
				remainder = chunk_at_offset(victim, nb);
#if CONSISTENCY == 1
				POS_WRITE_VAUE(name, (unsigned long *)&unsorted_chunks(av)->bk, (unsigned long)remainder);
				POS_WRITE_VAUE(name, (unsigned long *)&unsorted_chunks(av)->fd, (unsigned long)remainder);
				POS_WRITE_VAUE(name, (unsigned long *)&av->last_remainder, (unsigned long)remainder);
#else
				unsorted_chunks(av)->bk = unsorted_chunks(av)->fd = remainder;
				av->last_remainder = remainder;
#endif
				remainder->bk = remainder->fd = unsorted_chunks(av);
				if (!in_smallbin_range(remainder_size)) {
					remainder->fd_nextsize = NULL;
					remainder->bk_nextsize = NULL;
				}

// Remainder dosen't need logging...
				if (chunk_is_last(victim))
				{
					set_head(remainder, remainder_size | LAST_CHUNK | PREV_INUSE);
					//dk s
					//av->last_chunk_pointer = remainder;
					if((unsigned long)av->last_chunk_pointer < (unsigned long)remainder)
					{
						printf("remainder : %p, is_last : %lu\n", remainder, chunk_is_last(remainder));
						printf("victim : %p\n", victim);
						av->last_chunk_pointer = remainder;
					}
					//dk e
				}
				else
					set_head(remainder, remainder_size | PREV_INUSE);

				// set PREV_INUSE flag..
#if CONSISTENCY == 1
				if (chunk_is_first(victim)) {
					set_head_log(name, victim, nb | FIRST_CHUNK | PREV_INUSE);
				} else {
					set_head_log(name, victim, nb | PREV_INUSE);
				}
				
				set_foot_log(name, remainder, remainder_size);
#else
				if (chunk_is_first(victim))
					set_head(victim, nb | FIRST_CHUNK | PREV_INUSE);
				else
					set_head(victim, nb | PREV_INUSE);

				set_foot(remainder, remainder_size);
#endif
				void *p = chunk2mem(victim);

				return p;
			}

#if CONSISTENCY == 1
			POS_WRITE_VAUE(name, (unsigned long *)&unsorted_chunks(av)->bk, (unsigned long)bck);
			POS_WRITE_VAUE(name, (unsigned long *)&bck->fd, (unsigned long)unsorted_chunks(av));
#else
			unsorted_chunks(av)->bk = bck;
			bck->fd = unsorted_chunks(av);
#endif

			if (size == nb) {
#if CONSISTENCY == 1
				set_inuse_bit_at_offset_log(name, victim, size);
#else
				set_inuse_bit_at_offset(victim, size);
#endif
				void *p = chunk2mem(victim);

				return p;
			}

			if (in_smallbin_range(size)) {
				victim_index = smallbin_index(size);
				bck = bin_at(av, victim_index);
				fwd = bck->fd;
//sb s
printf("			unsorted -> small bin!\n");
printf("			size : %lu\n", size);
//sb e
			}
			else {
				victim_index = largebin_index(size);
				bck = bin_at(av, victim_index);
				fwd = bck->fd;

				if (fwd != bck) {
					size |= PREV_INUSE; //In order not to use chunksize()
					if ((unsigned long)(size) < (unsigned long)(bck->bk->size)) {
						fwd = bck;
						bck = bck->bk;

// Current victim was in the unsorted bin that fd_nextsize dosen't need.. so, we don't leave log.. (We don't leave log for fd_nextsize below..)
						victim->fd_nextsize = fwd->fd;
						victim->bk_nextsize = fwd->fd->bk_nextsize;
#if CONSISTENCY == 1
						POS_WRITE_VAUE(name, (unsigned long *)&fwd->fd->bk_nextsize, (unsigned long)victim);
						POS_WRITE_VAUE(name, (unsigned long *)&victim->bk_nextsize->fd_nextsize, (unsigned long)victim);
#else
						fwd->fd->bk_nextsize = victim->bk_nextsize->fd_nextsize = victim;
#endif
//sb s
printf("			unsorted -> large bin!\n");
//sb e
					}
					else {
						while ((unsigned long) size < fwd->size) {
							fwd = fwd->fd_nextsize;
						}

						if ((unsigned long) size == (unsigned long) fwd->size)
							fwd = fwd->fd;
						else {
							victim->fd_nextsize = fwd;
							victim->bk_nextsize = fwd->bk_nextsize;
#if CONSISTENCY == 1
							POS_WRITE_VAUE(name, (unsigned long *)&fwd->bk_nextsize, (unsigned long)victim);
							POS_WRITE_VAUE(name, (unsigned long *)&victim->bk_nextsize->fd_nextsize, (unsigned long)victim);
#else
							fwd->bk_nextsize = victim;
							victim->bk_nextsize->fd_nextsize = victim;
#endif
						}
						bck = fwd->bk;
					}
				} 
				else
					victim->fd_nextsize = victim->bk_nextsize = victim;
			}

#if CONSISTENCY == 1
			mark_bin_log(name, av, victim_index);
			POS_WRITE_VAUE(name, (unsigned long *)&victim->bk, (unsigned long)bck);
			POS_WRITE_VAUE(name, (unsigned long *)&victim->fd, (unsigned long)fwd);
			POS_WRITE_VAUE(name, (unsigned long *)&fwd->bk, (unsigned long)victim);
			POS_WRITE_VAUE(name, (unsigned long *)&bck->fd, (unsigned long)victim);
#else
			mark_bin(av, victim_index);
			victim->bk = bck;
			victim->fd = fwd;
			fwd->bk = victim;
			bck->fd = victim;
#endif

#define MAX_ITERS	10000
			if (++iters >= MAX_ITERS)
				break;
		}

		// 4. large bin (1024<=)
if(POS_DEBUG_MALLOC == 1)
{
	printf("large bin - size : %lu\n", (unsigned long)bytes);
}
		if (!in_smallbin_range(nb)) {

			bin = bin_at(av, idx);

			if ((victim = first(bin)) != bin &&
			   (unsigned long)(victim->size) >= (unsigned long)(nb)) {

				victim = victim->bk_nextsize;
				while (((unsigned long)(size = chunksize(victim)) < (unsigned long)(nb)))
					victim = victim->bk_nextsize;

				//if (victim != last(bin) && victim->size == victim->fd->size)
				if (victim != last(bin) && chunksize(victim) == chunksize(victim->fd))
					victim = victim->fd;

				remainder_size = size - nb;
#if CONSISTENCY == 1
				unlink_log(name, victim, bck, fwd);
#else
				unlink(victim, bck, fwd);
#endif

				if (remainder_size < MINSIZE)  {
#if CONSISTENCY == 1
					set_inuse_bit_at_offset_log(name, victim, size);
#else
					set_inuse_bit_at_offset(victim, size);
#endif
				}
				else {
					remainder = chunk_at_offset(victim, nb);

#if CONSISTENCY == 1
					insert_to_unsorted_log(name, av, remainder, bck, fwd, remainder_size);
#else
					insert_to_unsorted(av, remainder, bck, fwd, remainder_size);
#endif

// Remainder dosen't need logging...
					if (chunk_is_last(victim))
					{
						set_head(remainder, remainder_size | LAST_CHUNK | PREV_INUSE);
						//dk s
						//av->last_chunk_pointer = remainder;
						if((unsigned long)av->last_chunk_pointer < (unsigned long)remainder)
						{
							printf("remainder : %p, is_last : %lu\n", remainder, chunk_is_last(remainder));
							av->last_chunk_pointer = remainder;
						}
						//dk e
					}
					else
						set_head(remainder, remainder_size | PREV_INUSE);

					// set PREV_INUSE flag..
#if CONSISTENCY == 1
					if (chunk_is_first(victim)) {
						set_head_log(name, victim, nb | FIRST_CHUNK | PREV_INUSE);
					} else {
						set_head_log(name, victim, nb | PREV_INUSE);
					}
					
					set_foot_log(name, remainder, remainder_size);
#else
					if (chunk_is_first(victim))
						set_head(victim, nb | FIRST_CHUNK | PREV_INUSE);
					else
						set_head(victim, nb | PREV_INUSE);

					set_foot(remainder, remainder_size);
#endif
				}

				void *p = chunk2mem(victim);
				return p;
			}
		}

		// 5. large bin in next size
if(POS_DEBUG_MALLOC == 1)
{
	printf("large bin in next size - size : %lu\n", (unsigned long)bytes);
}
		++idx;
		bin = bin_at(av,idx);
		block = idx2block(idx);
		map = av->binmap[block];
	 	bit = idx2bit(idx);

		for (;;) {

			if (bit > map || bit == 0) {
				do {
					if (++block >= BINMAPSIZE)
						goto new_alloc;
				} while ( (map = av->binmap[block]) == 0);

				bin = bin_at(av, (block << BINMAPSHIFT));
				bit = 1;
			}

			while ((bit & map) == 0) {
				bin = next_bin(bin);
				bit <<= 1;
			}

			victim = last(bin);

			if (victim == bin) {

#if CONSISTENCY == 1
				POS_WRITE_VAUE(name, (unsigned long *)&av->binmap[block], (unsigned long)(map &~bit));
#else
				av->binmap[block] = map &= ~bit;
#endif
				bin = next_bin(bin);
		        	bit <<= 1;
			}
			else {
				size = chunksize(victim);

				remainder_size = size - nb;

#if CONSISTENCY == 1
				unlink_log(name, victim, bck, fwd);
#else
				unlink(victim, bck, fwd);
#endif

				if (remainder_size < MINSIZE) {
#if CONSISTENCY == 1
					set_inuse_bit_at_offset_log(name, victim, size);
#else
					set_inuse_bit_at_offset(victim, size);
#endif
				}
				else {
					remainder = chunk_at_offset(victim, nb);

#if CONSISTENCY == 1
					insert_to_unsorted_log(name, av, remainder, bck, fwd, remainder_size);
#else
					insert_to_unsorted(av, remainder, bck, fwd, remainder_size);
#endif

					if (in_smallbin_range(nb)) {
#if CONSISTENCY == 1
						POS_WRITE_VAUE(name, (unsigned long *)&av->last_remainder, (unsigned long)remainder);
#else
						av->last_remainder = remainder;
#endif
					}

					if (chunk_is_last(victim))
					{
						set_head(remainder, remainder_size | LAST_CHUNK | PREV_INUSE);
						//dk s
						//av->last_chunk_pointer = remainder;
						if((unsigned long)av->last_chunk_pointer < (unsigned long)remainder)
						{
							printf("remainder : %p, is_last : %lu\n", remainder, chunk_is_last(remainder));
							av->last_chunk_pointer = remainder;
						}
						//dk e
					}
					else
						set_head(remainder, remainder_size | PREV_INUSE);

					// set PREV_INUSE flag..
#if CONSISTENCY == 1
					if (chunk_is_first(victim)) {
						set_head_log(name, victim, nb | FIRST_CHUNK | PREV_INUSE);
					} else {
						set_head_log(name, victim, nb | PREV_INUSE);
					}
					
					set_foot_log(name, remainder, remainder_size);
#else
					if (chunk_is_first(victim))
						set_head(victim, nb | FIRST_CHUNK | PREV_INUSE);
					else
						set_head(victim, nb | PREV_INUSE);

					set_foot(remainder, remainder_size);
#endif
				}

				void *p = chunk2mem(victim);
				return p;
			}
		}

new_alloc:
printf("	new_alloc num : %d\n", ++new_alloc_count);
		// 6. new allocation
if(POS_DEBUG_MALLOC == 1)
{
	printf("new allocation - size : %lu\n", (unsigned long)bytes);
	printf("gc_result : %d\n", gc_result);
}	
		if(gc_result < 1)
		{
			pos_local_gc(name);		
			gc_result++;
		}
		
		if(gc_result == 1)
		{
			gc_result++;
			goto first;
		}

		//size = (nb + MINSIZE +2*SIZE_SZ + pagemask) & ~pagemask;
		//dk s
		size = (nb + MINSIZE +4*SIZE_SZ + pagemask) & ~pagemask;
		//dk e
		//dk s
		//size += DEFAULT_PAD;
		//dk e
		//char* mm = (char*)(SEG_ALLOC(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE));
		char *mm = (char *)pos_seg_alloc(name, size);
		memset(mm , 0 , size);
		printf("[new alloc] nb : %lu, MINSIZE : %lu, pagemask : %lu, SIZE_SZ : %lu\n", nb, MINSIZE, pagemask, SIZE_SZ);
		printf("DEFAULT_PAD : %d\n", DEFAULT_PAD);
		printf("	new seg alloc count : %d\n", ++seg_alloc_count);
		printf("	new segment start address : %p\n", mm);
		printf("	size : %lu\n", (unsigned long)size);
		total_chunks_size = 0;
#if CONSISTENCY == 1
		pos_log_insert_malloc_free(name, (unsigned long)mm, size);
#endif
		//if (mm != MAP_FAILED) {
		if (mm != (char *)0) {
#if CONSISTENCY == 1
			POS_WRITE_VAUE(name, (unsigned long *)&av->system_mem, (unsigned long)(av->system_mem+size));
#else
			av->system_mem += size;
#endif

			mchunkptr p;

			p = (mchunkptr)mm;

			//remainder_size = size - nb - 2*SIZE_SZ;
			//dk s
			remainder_size = size - nb - 4*SIZE_SZ;
			//dk e
			remainder = chunk_at_offset(p, nb);

#if CONSISTENCY == 1
			insert_to_unsorted_log(name, av, remainder, bck, fwd, remainder_size);
#else
			insert_to_unsorted(av, remainder, bck, fwd, remainder_size);
#endif

			/*if (in_smallbin_range(nb))
				av->last_remainder = remainder;*/

			// set PREV_INUSE flag..
//#if CONSISTENCY == 1
//#elseif
			set_head(p, nb | FIRST_CHUNK | PREV_INUSE);
			set_head(remainder, remainder_size | LAST_CHUNK | PREV_INUSE);

			set_foot(remainder, remainder_size);
			clear_inuse_bit_at_offset(remainder, remainder_size);
			printf("remainder : %p, is_last : %lu\n", remainder, chunk_is_last(remainder));
			//dk s
			present_last_chunk = av->last_chunk_pointer;
		printf("present_last_chunk : %p, size : %lu\n", present_last_chunk, chunksize(present_last_chunk));
		//set_next_seg_pointer(present_last_chunk, chunksize(present_last_chunk), 12345); //insert next seg pointer at present last chunk
mchunkptr p_tmp;
//int temp;
//		temp = 12345;
		p_tmp = next_seg(present_last_chunk, chunksize(present_last_chunk));
		memcpy((mchunkptr)((char *)p_tmp+SIZE_SZ), &p, sizeof(unsigned long));
		printf("p : %p\n", p);

	printf("last + last_size : %lu\n", chunksize((mchunkptr)((char *)present_last_chunk + chunksize(present_last_chunk))));
		printf("last + last_size + size_sz : %lu\n", chunksize((mchunkptr)((char *)present_last_chunk + chunksize(present_last_chunk) + SIZE_SZ)));
		printf("last + last_size + size_sz*2 : %lu\n", chunksize((mchunkptr)((char *)present_last_chunk + chunksize(present_last_chunk)+ (SIZE_SZ*2))));


	//	printf("p_tmp : %p\n", p_tmp);
	//	printf("p_tmp : %lu\n", chunksize(p_tmp));
			set_next_seg_pointer(remainder, remainder_size, 0); //initialize next seg pointer field
			//((mchunkptr)((char*)(remainder)+(reminder_size)))->size = 0; // next segment pointer
printf("prsent last chunk size : %lu, remainder chunk size : %lu\n", chunksize(present_last_chunk), chunksize(remainder));
			//mchunkptr plc_addr, ng_addr;
			//plc_addr = (mchunkptr)(chunksize(present_last_chunk)+(char *)present_last_chunk);
			//ng_addr = (mchunkptr)chunksize(remainder)+remainder;
			//printf("plc addr : %p, ng_addr : %p\n", plc_addr, ng_addr);
			printf("present : %lu, next seg : %lu\n", chunksize((mchunkptr)((char *)present_last_chunk+SIZE_SZ+chunksize(present_last_chunk))), chunksize((mchunkptr)((char *)remainder+SIZE_SZ+chunksize(remainder))));
	
			av->last_chunk_pointer = remainder;
  			//dk e
//#endif
			//return p;
			return chunk2mem(p);
		} 
		else
			return 0;
  
	}
}


/*
  ------------------------------ pos_free ------------------------------
*/
//static void 
void
pos_int_free(char *name, mstate av, mchunkptr p, int flag)
{
	INTERNAL_SIZE_T size;
	mfastbinptr* fb;
	mchunkptr prevchunk;
	INTERNAL_SIZE_T prevsize;
	mchunkptr nextchunk;
	INTERNAL_SIZE_T nextsize;
	int nextinuse;
	mchunkptr bck;
	mchunkptr fwd;

	//const char *errstr = NULL;


	size = chunksize(p);
	/*if ((uintptr_t) p > (uintptr_t) -size || misaligned_chunk (p)) {
		errstr = "free(): invalid pointer";
errout:
		//malloc_printerr (check_action, errstr, chunk2mem(p));
		return;
	}*/
	/*if (size < MINSIZE) {
		errstr = "free(): invalid size";
		goto errout;
	}*/

	//check_inuse_chunk(av, p);


	// fastbin
	if (flag==1 && (unsigned long)(size) <= (unsigned long)(get_max_fast ())) {
		/*if (chunk_at_offset (p, size)->size <= 2 * SIZE_SZ
		   || chunksize (chunk_at_offset (p, size)) >= av->system_mem) {
			errstr = "free(): invalid next size (fast)";
			goto errout;
		}*/

#if CONSISTENCY == 1
		set_fastchunks_log(name, av);
#else
		set_fastchunks(av);
#endif
		fb = &fastbin(av, fastbin_index(size));

		if (*fb == p) {
			//errstr = "double free or corruption (fasttop)";
			//goto errout;
			return ;
		}

#if CONSISTENCY == 1
		POS_WRITE_VAUE(name, (unsigned long *)&p->fd, (unsigned long)*fb);
		POS_WRITE_VAUE(name, (unsigned long *)fb, (unsigned long)p);
#else
		p->fd = *fb;
		*fb = p;
#endif
		////dk s
		clear_inuse_bit_at_offset(p, size);
		printf("is fast bin chunk [%p] in-use? : %d\n", p, inuse(p));
		////dk e

		return ;
	}

	// 1. First chunk
	if (chunk_is_first(p)) {

		nextchunk = next_chunk(p);
		nextsize = chunksize(nextchunk);

		// 1-1. (free F), free L
		if (chunk_is_last(nextchunk) && !inuse(nextchunk)) {

			//if (av < p && p < (char *)(av+PAGESIZE)){
			if ((char*)av+sizeof(struct malloc_state) == (char*)p) {
#if CONSISTENCY == 1
				insert_to_unsorted_log(name, av, p, bck, fwd, size);

				set_foot_log(name, p, size);
				clear_inuse_bit_at_offset_log(name, p, size);
#else
				insert_to_unsorted(av, p, bck, fwd, size);

				set_foot(p, size);
				clear_inuse_bit_at_offset(p, size);
#endif

				goto out;
			}
			else {
#if CONSISTENCY == 1
				unlink_log(name, nextchunk, bck, fwd);
				size = size + nextsize + 2*SIZE_SZ;

				pos_log_insert_malloc_free(name, (unsigned long)p, size);
				//pos_seg_free(name, (void *)p, size); // Delayed pos_seg_free
				POS_WRITE_VAUE(name, (unsigned long *)&av->system_mem, (unsigned long)(av->system_mem-size));
#else
				unlink(nextchunk, bck, fwd);
				size = size + nextsize + 2*SIZE_SZ;
				/*if (size%PAGESIZE != 0) {
					errstr = "free(): unmmap size is not page size";
					goto errout;
				}*/
				//FREE((char*)p, size);
				pos_seg_free(name, (void *)p, size);
				av->system_mem -= size;
#endif

				goto out;
			}

		}

		// 1-3. (free F), free M
		else if (!inuse(nextchunk)) {
#if CONSISTENCY == 1
			unlink_log(name, nextchunk, bck, fwd);
			size += nextsize;

			insert_to_unsorted_log(name, av, p, bck, fwd, size);

			set_head_log(name, p, size | FIRST_CHUNK | PREV_INUSE);
			set_foot_log(name, p, size);
#else
			unlink(nextchunk, bck, fwd);
			size += nextsize;

			insert_to_unsorted(av, p, bck, fwd, size);

			set_head(p, size | FIRST_CHUNK | PREV_INUSE);
			set_foot(p, size);
#endif

			goto out;
		}

		// 1-2. (free F), inuse L & 1-4. (free F), inuse M
		else {
#if CONSISTENCY == 1
			insert_to_unsorted_log(name, av, p, bck, fwd, size);

			set_foot_log(name, p, size);
			clear_inuse_bit_at_offset_log(name, p, size);
#else
			insert_to_unsorted(av, p, bck, fwd, size);

			set_foot(p, size);
			clear_inuse_bit_at_offset(p, size);
#endif

			goto out;
		}

	}

	// 2. Last chunk
	else if (chunk_is_last(p)) {

		if (!prev_inuse(p)) {
			prevchunk = prev_chunk(p);
			prevsize = chunksize(prevchunk);

			// 2-1. free F, (free L)
			if (chunk_is_first(prevchunk)) {

				//if (av < prevchunk && prevchunk < av+PAGESIZE){
				if((char*)av+sizeof(struct malloc_state) == (char*)prevchunk) {
#if CONSISTENCY == 1
					insert_to_unsorted_log(name, av, p, bck, fwd, size);
  
					set_foot_log(name, p, size);
					clear_inuse_bit_at_offset_log(name, p, size);
#else
					insert_to_unsorted(av, p, bck, fwd, size);
  
					set_foot(p, size);
					clear_inuse_bit_at_offset(p, size);
#endif

					goto out;
				}
				else {
#if CONSISTENCY == 1
					unlink_log(name, prevchunk, bck, fwd);
					size = prevsize+size+2*SIZE_SZ;
					//pos_seg_free(name, (void *)p, size);
					pos_log_insert_malloc_free(name, (unsigned long)p, size);
					POS_WRITE_VAUE(name, (unsigned long *)&av->system_mem, (unsigned long)(av->system_mem-size));
#else
					unlink(prevchunk, bck, fwd);
					size = prevsize+size+2*SIZE_SZ;
					/*if (size%PAGESIZE != 0) {
						errstr = "free(): unmmap size is not page size";
						goto errout;
					}*/
					//FREE((char*)p, size);
					pos_seg_free(name, (void *)p, size);
					av->system_mem -= size;
#endif

					goto out;
				}

			}

			// 2-3. free M, (free L)
			else {
#if CONSISTENCY == 1
				unlink_log(name, prevchunk, bck, fwd);
				size += prevsize;
				p = chunk_at_offset(p, -((long) prevsize));
  
				insert_to_unsorted_log(name, av, p, bck, fwd, size);

				set_head_log(name, p, size | LAST_CHUNK | PREV_INUSE);
				set_foot_log(name, p, size);
				clear_inuse_bit_at_offset_log(name, p, size);
#else
				unlink(prevchunk, bck, fwd);
				size += prevsize;
				p = chunk_at_offset(p, -((long) prevsize));
  
				insert_to_unsorted(av, p, bck, fwd, size);

				set_head(p, size | LAST_CHUNK | PREV_INUSE);
				//dk s
				//av->last_chunk_pointer = p;
				if((unsigned long)av->last_chunk_pointer < (unsigned long)p)
				{
					printf("remainder : %p, is_last : %lu\n", p, chunk_is_last(p));
					av->last_chunk_pointer = p;
				}
				//dk e
				set_foot(p, size);
				clear_inuse_bit_at_offset(p, size);
#endif
				goto out;
			}

		}

		// 2-2. inuse F, (free L) & 2-4. inuse M, (free L)
		else {
#if CONSISTENCY == 1
			insert_to_unsorted_log(name, av, p, bck, fwd, size);
  
			set_foot_log(name, p, size);
			clear_inuse_bit_at_offset_log(name, p, size);
#else
			insert_to_unsorted(av, p, bck, fwd, size);
  
			set_foot(p, size);
			clear_inuse_bit_at_offset(p, size);
#endif

			goto out;
		}

	}

	// 3. Middle chunk
	else {

		nextchunk = next_chunk(p);
		nextsize = chunksize(nextchunk);

		if (!prev_inuse(p)) {
			prevchunk = prev_chunk(p);
			prevsize = chunksize(prevchunk);

			// 3-1. free F, (free M), free L
			if (chunk_is_first(prevchunk) && chunk_is_last(nextchunk) && !inuse(nextchunk) ) {
	
				//if (av < prevchunk && prevchunk < av+PAGESIZE){
				if((char*)av+sizeof(struct malloc_state) == (char*)prevchunk) {
#if CONSISTENCY == 1
					unlink_log(name, prevchunk, bck, fwd);
					size += prevsize;
					p = chunk_at_offset(p, -((long) prevsize));

					insert_to_unsorted_log(name, av, p, bck, fwd, size);
  
					set_head_log(name, p, size | FIRST_CHUNK | PREV_INUSE);
					set_foot_log(name, p, size);
					clear_inuse_bit_at_offset_log(name, p, size);
#else
					unlink(prevchunk, bck, fwd);
					size += prevsize;
					p = chunk_at_offset(p, -((long) prevsize));

					insert_to_unsorted(av, p, bck, fwd, size);
  
					set_head(p, size | FIRST_CHUNK | PREV_INUSE);
					set_foot(p, size);
					clear_inuse_bit_at_offset(p, size);
  #endif
  
					goto out;
				}

				else {
#if CONSISTENCY == 1
					unlink_log(name, prevchunk, bck, fwd);
					unlink_log(name, nextchunk, bck, fwd);
					p = chunk_at_offset(p, -((long) prevsize));
					size = prevsize+size+nextsize+2*SIZE_SZ;

					pos_log_insert_malloc_free(name, (unsigned long)p, size);
					//pos_seg_free(name, (void *)p, size);
					POS_WRITE_VAUE(name, (unsigned long *)&av->system_mem, (unsigned long)(av->system_mem-size));
#else
					unlink(prevchunk, bck, fwd);
					unlink(nextchunk, bck, fwd);
					p = chunk_at_offset(p, -((long) prevsize));
					size = prevsize+size+nextsize+2*SIZE_SZ;
					/*if (size%PAGESIZE != 0) {
						errstr = "free(): unmmap size is not page size";
						goto errout;
					}*/
					//FREE((char*)p, size);
					pos_seg_free(name, (void *)p, size);
					av->system_mem -= size;
  #endif
  
					goto out;
				}
			}

#if CONSISTENCY == 1
			unlink_log(name, prevchunk, bck, fwd);
#else
			unlink(prevchunk, bck, fwd);
#endif
			size += prevsize;
			p = chunk_at_offset(p, -((long) prevsize));

			if (chunk_is_first(prevchunk)) {
#if CONSISTENCY == 1
				set_head_log(name, p, size | FIRST_CHUNK | PREV_INUSE);
#else
				set_head(p, size | FIRST_CHUNK | PREV_INUSE);
				//set_foot(p, size);
				//clear_inuse_bit_at_offset(p, size);
#endif
			}

		}

		nextinuse = inuse_bit_at_offset(nextchunk, nextsize);

		if (!nextinuse) {
#if CONSISTENCY == 1
			unlink_log(name, nextchunk, bck, fwd);
#else
			unlink(nextchunk, bck, fwd);
#endif
			size += nextsize;
		}

#if CONSISTENCY == 1
		insert_to_unsorted_log(name, av, p, bck, fwd, size);

		if (chunk_is_first(p)) {
			set_head_log(name, p, size | FIRST_CHUNK | PREV_INUSE);
		} else if (chunk_is_last(nextchunk)&&!nextinuse) {
			set_head_log(name, p, size | LAST_CHUNK | PREV_INUSE);
		} else {
			set_head_log(name, p, size | PREV_INUSE);
		}
		set_foot_log(name, p, size);
		clear_inuse_bit_at_offset_log(name, p, size);
#else
		//else
		//clear_inuse_bit_at_offset(nextchunk, 0);

		insert_to_unsorted(av, p, bck, fwd, size);

		if (chunk_is_first(p)) {
			set_head(p, size | FIRST_CHUNK | PREV_INUSE);
		} else if (chunk_is_last(nextchunk)&&!nextinuse) {
			set_head(p, size | LAST_CHUNK | PREV_INUSE);
			//dk s
			//av->last_chunk_pointer = p;
			if((unsigned long)av->last_chunk_pointer < (unsigned long)p)
			{
				printf("remainder : %p, is_last : %lu\n", p, chunk_is_last(p));
				av->last_chunk_pointer = p;
			}
			//dk e
		} else {
			set_head(p, size | PREV_INUSE);
		}
		set_foot(p, size);
		clear_inuse_bit_at_offset(p, size);

		//check_free_chunk(av, p);
 #endif
	}

out: 
	if ((unsigned long)(size) >= FASTBIN_CONSOLIDATION_THRESHOLD && have_fastchunks(av)) {
		pos_malloc_consolidate(name, av);
	}
}


/*
  ------------------------------ pos_realloc ------------------------------
*/

////////////////////////////////////////
// WARNING!: pos_realloc has error!. FIX UP!
////////////////////////////////////////
Void_t*
pos_int_realloc(char *name, mstate av, mchunkptr oldp, INTERNAL_SIZE_T oldsize,
	     INTERNAL_SIZE_T nb)
{
	mchunkptr newp;				/* chunk to return */
	INTERNAL_SIZE_T newsize;		/* its size */
	Void_t* newmem;				/* corresponding user mem */

	mchunkptr next;				/* next contiguous chunk after oldp */

	mchunkptr remainder;			/* extra space at end of newp */
	unsigned long remainder_size;	/* its size */

	mchunkptr bck;				/* misc temp for linking */
	mchunkptr fwd;				/* misc temp for linking */

	unsigned long copysize;		/* bytes to copy */
	unsigned int ncopies;			/* INTERNAL_SIZE_T words to copy */
	INTERNAL_SIZE_T* s;			/* copy source */
	INTERNAL_SIZE_T* d;			/* copy destination */

	const char *errstr = NULL;


	/* oldmem size */
	/*if (oldp->size <= 2 * SIZE_SZ || oldsize >= av->system_mem) {
		errstr = "realloc(): invalid old size";
errout:
		malloc_printerr (check_action, errstr, chunk2mem(oldp));
		return NULL;
	}*/

	next = chunk_at_offset(oldp, oldsize);
	INTERNAL_SIZE_T nextsize = chunksize(next);
	/*if (next->size <= 2 * SIZE_SZ || nextsize >= av->system_mem) {
		errstr = "realloc(): invalid next size";
			goto errout;
	}*/

	//old size 보다 작을 경우
	if ((unsigned long)(oldsize) >= (unsigned long)(nb)) {
		/* already big enough; split below */
		newp = oldp;
		newsize = oldsize;
	}

	//old size 보다 클 경우
	else {

		/* Try to expand forward into next chunk;  split off remainder below */
		if (!inuse(next) &&
			(unsigned long)(newsize = oldsize + nextsize) >= (unsigned long)(nb)) {
			newp = oldp;
			unlink(next, bck, fwd);
		}

		/* allocate, copy, free */
		else {
			newmem = pos_int_malloc(name, av, nb - MALLOC_ALIGN_MASK);
			if (newmem == 0)
				return 0; /* propagate failure */

			newp = mem2chunk(newmem);
			newsize = chunksize(newp);

			/*
			  Avoid copy if newp is next chunk after oldp.
			*/
			if (newp == next) {
				newsize += oldsize;
				newp = oldp;
			}
			else {
				/*
				  Unroll copy of <= 36 bytes (72 if 8byte sizes)
				  We know that contents have an odd number of
				  INTERNAL_SIZE_T-sized words; minimally 3.
				*/

				copysize = oldsize - SIZE_SZ;
				s = (INTERNAL_SIZE_T*)(chunk2mem(oldp));
				d = (INTERNAL_SIZE_T*)(newmem);
				ncopies = copysize / sizeof(INTERNAL_SIZE_T);

				if (ncopies > 9)
					memcpy(d, s, copysize);
				else {
					*(d+0) = *(s+0);
					*(d+1) = *(s+1);
					*(d+2) = *(s+2);
					if (ncopies > 4) {
						*(d+3) = *(s+3);
						*(d+4) = *(s+4);
						if (ncopies > 6) {
							*(d+5) = *(s+5);
							*(d+6) = *(s+6);
							if (ncopies > 8) {
								*(d+7) = *(s+7);
								*(d+8) = *(s+8);
							}
						}
					}
				}

				pos_int_free(name, av, oldp, 1);

				return chunk2mem(newp);
			}
		}
	}

	/* If possible, free extra space in old or extended chunk */

	remainder_size = newsize - nb;

	if (remainder_size < MINSIZE) { /* not enough extra to split off */
		set_head_size(newp, newsize);
		set_inuse_bit_at_offset(newp, newsize);
	}
	else { /* split remainder */
		remainder = chunk_at_offset(newp, nb);

		if (chunk_is_last(newp))
		{
			set_head(remainder, remainder_size | LAST_CHUNK | PREV_INUSE);
			//dk s
			//av->last_chunk_pointer = remainder;
			if((unsigned long)av->last_chunk_pointer < (unsigned long)remainder)
			{
				printf("remainder : %p, is_last : %lu\n", remainder, chunk_is_last(remainder));
				av->last_chunk_pointer = remainder;
			}
			//dk e
		}
		else
			set_head(remainder, remainder_size | PREV_INUSE);

		// set PREV_INUSE flag..
		if (chunk_is_first(newp))
			set_head(newp, nb | FIRST_CHUNK | PREV_INUSE);
		else
			set_head(newp, nb | PREV_INUSE);
		
		//set_head_size(newp, nb);
		//set_head(remainder, remainder_size | PREV_INUSE |(av != &main_arena ? NON_MAIN_ARENA : 0));

		/* Mark remainder as inuse so free() won't complain */
		set_inuse_bit_at_offset(remainder, remainder_size);
		pos_int_free(name, av, remainder, 1);
	}

	return chunk2mem(newp);
}



/*
  ------------------------------ pos_malloc_init_state ------------------------------
*/

//static void 
void
pos_malloc_init_state(char *name, mstate av)
{
	mchunkptr	first_chunk;
	unsigned long	first_size;
	mchunkptr	last_chunk;
	unsigned long	last_size;

	mchunkptr       bck;
	mchunkptr       fwd;

	int	i;
	mbinptr	bin;


	// init mutex key

	// initialize malloc_state
#if CONSISTENCY == 1
	set_init_key_log(name, av);
#else
	set_init_key(av);
#endif

// Below codes don't need logging.

	for (i=1; i < NBINS; i++) {
		bin = bin_at(av,i);
		bin->fd = bin->bk = bin;
	}

	//set_max_fast(DEFAULT_MXFAST);
	clear_fastchunks(av);
	for (i=0; i< NFASTBINS ; i++) {
		av->fastbinsY[i] = 0;
	}

	// first chunk
	first_chunk = chunk_at_offset(av, sizeof(struct malloc_state));
	//first_size = (PAGESIZE - sizeof(struct malloc_state) - 2*SIZE_SZ)/2;	// 956
	//dk s
	first_size = (PAGESIZE - sizeof(struct malloc_state) - 4*SIZE_SZ)/2;	// 4kb - 2172 - 16
	first_size = 0;
	printf("mstate size = %lu\n", sizeof(struct malloc_state));
	printf("page size = %d\n", PAGESIZE);
	printf("first chunk size = %lu\n", first_size);
	printf("SIZE_SZ size = %lu\n", SIZE_SZ);
	//dk e
   

//#if CONSISTENCY == 1
	//first_size = (128*1024-1)*4096 + 960; //536867776
	//first_size = request2size(first_size); // 536867792
//#else
	//dk s
//	first_size = 0;
	//dk e
//#endif
//insert_to_unsorted(av, first_chunk, bck, fwd, first_size);

	set_head(first_chunk, first_size | FIRST_CHUNK | PREV_INUSE);
	set_foot(first_chunk, first_size);
	clear_inuse_bit_at_offset(first_chunk, first_size);
	//dk s
	//insert_to_unsorted(av, first_chunk, bck, fwd, first_size);
	//dk e

	// last_chunk
	last_chunk = chunk_at_offset(first_chunk, first_size);
	//last_size = first_size;
//#if CONSISTENCY == 1
	////last_size = (128*1024)*4096 + 944; // 536867760
	//last_size = (256*1024)*4096 - first_size - 2*SIZE_SZ;
	//last_size = request2size(last_size); // 536874032
//#else
//	last_size = 988;
	//dk s
	//last_size = 1456;
	//last_size = first_size;
	last_size = 1480;
	//dk e
//#endif
	insert_to_unsorted(av, last_chunk, bck, fwd, last_size);

	set_head(last_chunk, last_size | LAST_CHUNK | PREV_INUSE);
	//dk s
	//av->last_chunk_pointer = last_chunk;
	//if((unsigned long)av->last_chunk_pointer < (unsigned long)last_chunk)
	//{
		printf("remainder : %p, is_last : %lu\n", last_chunk, chunk_is_last(last_chunk));
		av->last_chunk_pointer = last_chunk;
	//}
	//dk e
	set_foot(last_chunk, last_size);
	clear_inuse_bit_at_offset(last_chunk, last_size);

	//dk s
	set_next_seg_pointer(last_chunk, last_size, 0);
	mchunkptr temp_p;
	temp_p = next_seg(last_chunk, last_size);
	printf("temp_p : %p\n", temp_p);
	temp_p = (mchunkptr)((char*)last_chunk + chunksize(last_chunk)+SIZE_SZ);
	printf("next seg : %lu\n", chunksize(temp_p));
	
	//dk e
	av->last_remainder = 0;
	for (i=0; i<BINMAPSIZE; i++) {
		av->binmap[i] = 0;
	}
	av->system_mem = PAGESIZE;

	av->prime_obj = NULL;

	//sb s
	//av->total_alloc_size = 0;
	//sb e
}



/*
  ------------------------------ pos_malloc_wrapper ------------------------------
*/

//dk start
Void_t*
pos_public_mALLOc(char *name, unsigned long _bytes) 
{
	struct malloc_state *ar_ptr;
	Void_t *victim = NULL;
	size_t bytes = _bytes;

	/*if (pos_is_mapped(name) == 0) {
		//printf("Not mapped\n");
		return NULL;
	}*/
printf("[%d]current total chunk size : %lu\n", total_idx, total_chunks_size);
	ar_ptr = (struct malloc_state *)pos_lookup_mstate(name);
	if (ar_ptr == NULL) {
		return NULL;
	}
//sb s
	//printf("	current total allocation size : %lu\n", ar_ptr->total_alloc_size);
//sb e
	//if(*((size_t *)ar_ptr) == POS_MAGIC)
	if (!have_init_key(ar_ptr)) {
		pos_malloc_init_state(name, ar_ptr);
	}

	(void)mutex_lock(&ar_ptr->mutex);
	victim = pos_int_malloc(name, ar_ptr, bytes);
	(void)mutex_unlock(&ar_ptr->mutex);

total_chunks_size += chunksize(mem2chunk(victim));

	return victim;
}
//dk end

/*
  ------------------------------ pos_free_wrapper ------------------------------
*/

void
pos_public_fREe(char *name, Void_t *mem)
{
	struct malloc_state * ar_ptr;
	mchunkptr p;


	/*if (pos_is_mapped(name) == 0) {
		//printf("Not mapped\n");
		return ;
	}*/
	//memcpy(mem, 0, sizeof(mem));

	if (mem == (Void_t *)0)
		return;

	p = mem2chunk(mem);

	ar_ptr = (struct malloc_state *)pos_lookup_mstate(name);
	if (ar_ptr == NULL) {
		return;
	}

	printf("[free] p : %p, mem : %p\n", p, mem);
	(void)mutex_lock(&ar_ptr->mutex);
	pos_int_free(name, ar_ptr, p, 1);
	(void)mutex_unlock(&ar_ptr->mutex);
}


/*
  ------------------------------ pos_realloc_wrapper ------------------------------
*/

Void_t*
pos_public_rEALLOc(char *name, Void_t *oldmem, unsigned long _bytes)
{
	mstate ar_ptr;
	INTERNAL_SIZE_T nb;      /* padded request size */

	Void_t* newp;             /* chunk to return */

	size_t bytes = _bytes;

	/*if (bytes == 0 && oldmem != NULL) {
		pos_public_fREe(name, oldmem);
		return NULL;
	}*/

	/* realloc of null is supposed to be same as malloc */
	if (oldmem == 0)
		return pos_public_mALLOc(name, bytes);


	/* chunk corresponding to oldmem */
	const mchunkptr oldp = mem2chunk(oldmem);
	/* its size */
	const INTERNAL_SIZE_T oldsize = chunksize(oldp);

	/* Little security check which won't hurt performance: the
	    allocator never wrapps around at the end of the address space.
	    Therefore we can exclude some size values which might appear
	    here by accident or by "design" from some intruder. */
	/*if (__builtin_expect ((uintptr_t) oldp > (uintptr_t) -oldsize, 0)
		|| __builtin_expect (misaligned_chunk (oldp), 0))
	{
		malloc_printerr (check_action, "realloc(): invalid pointer", oldmem);
		return NULL;
	}*/

	checked_request2size(bytes, nb);

	ar_ptr = (struct malloc_state *)pos_lookup_mstate(name);
	if (ar_ptr == NULL) {
		return NULL;
	}

	(void)mutex_lock(&ar_ptr->mutex);
	newp = pos_int_realloc(name, ar_ptr, oldp, oldsize, nb);
	(void)mutex_unlock(&ar_ptr->mutex);

	if (newp == NULL) {
		/* Try harder to allocate memory in other arenas.  */
		newp = pos_public_mALLOc(name, bytes);
		if (newp != NULL) {
			memcpy (newp, oldmem, oldsize - SIZE_SZ);
			
			(void)mutex_lock(&ar_ptr->mutex);
			pos_int_free(name, ar_ptr, oldp, 1);
			(void)mutex_unlock(&ar_ptr->mutex);
		}
	}

	return newp;
}


/*
  ------------------------------ pos_set_prime_object ------------------------------
*/

void
pos_set_prime_object(char *name, void *obj)
{
	struct malloc_state *ar_ptr;

	ar_ptr = (struct malloc_state *)pos_lookup_mstate(name);
	if (ar_ptr == NULL) {
		return;
	}

	ar_ptr->prime_obj = obj;
/*#if CONSISTENCY == 1
	POS_WRITE_VAUE(name, (unsigned long *)&ar_ptr->prime_obj, (unsigned long)obj);
#else
	ar_ptr->prime_obj = obj;
#endif*/ 
}


void *
pos_get_prime_object(char *name)
{
	struct malloc_state *ar_ptr;

	ar_ptr = (struct malloc_state *)pos_lookup_mstate(name);
	if (ar_ptr == NULL) {
		return NULL;
	}

	return ar_ptr->prime_obj;
}


/*
  ------------------------------ pos_test_function ------------------------------
*/
void
pos_print_free_chunks(char *name)
{
	struct malloc_state * av;
	mbinptr	bin;
	mchunkptr p;
	int i;


	av = (struct malloc_state *)pos_lookup_mstate(name);
	if (av == NULL || !have_init_key(av)) {
		printf("\n     Can't print free chunks.\n");
		return;
	}

	printf("\n      ******************** Free chunks of `%s` ************************\n", name);

	printf("      *  1. Fast bins(0~7)\n");
	for (i=0; i< NFASTBINS ; i++) {
		p = (mchunkptr)fastbin(av, i);
		while (p!=0) {
			printf("      *    [%3d] addr=0x%lX, size=%lu", i, (unsigned long int)p, (unsigned long int)chunksize(p));
			if (chunk_is_first(p)) printf("(F)\n");
			else if (chunk_is_last(p)) printf("(L)\n");
			else printf("(M)\n");
			p = p->fd;
		}
	}

	printf("      *  2. Unsorted(1) / Small(2~63) / Large(64~128) bins\n");
	for (i=1; i < NBINS; i++) {
		bin = bin_at(av,i);
		p = bin->fd;
		while (p != bin) {
			printf("      *    [%3d] addr=0x%lX, size=%lu", i, (unsigned long int)p, (unsigned long int)chunksize(p));
			if (chunk_is_first(p)) printf("(F)\n");
			else if (chunk_is_last(p)) printf("(L)\n");
			else printf("(M)\n");
			p = p->fd;
		}
	}

	printf("      ***********************************************************************\n");
}

/*

  ------------------------------ pos_unsafe_pointer_function ------------------------------
*/

/* HEAPO (Cheolhee Lee) */

// Call unsafe Pointer Check
void pos_check_unsafe_pointer(char *name)
{
    struct malloc_state * av;
    struct pos_name_entry *name_entry;
    mchunkptr chunk_ptr;
    struct seg_info *head;
    int i = 1;

    av = (struct malloc_state *)pos_lookup_mstate(name);	
    if(av == NULL)   
        printf("Not Allocate Memory\n");

    name_entry = pos_lookup_name_entry(name);
    name_entry->seg_head = (struct seg_info *)malloc(sizeof(struct seg_info) * 1024);
    memset(name_entry -> seg_head, 0, sizeof(struct seg_info) * 1024);

    //Search Seg_info System call
    syscall(306 , name , name_entry -> seg_head);
    head = name_entry -> seg_head;	

    // First Segment
    chunk_ptr = chunk_at_offset(av, sizeof(struct malloc_state));
    pos_check_unsafe_segment(name, av, head, chunk_ptr);

    // Other Segment
    while(head[i].addr != 0)
    {
        pos_check_unsafe_segment(name, av, head, (Void_t *)head[i].addr);
	i++;
    }
}

void pos_check_unsafe_segment(char *name, mstate av, struct seg_info *head, Void_t *first_chunk)
{
    mchunkptr chunk_ptr;

    chunk_ptr = (mchunkptr)first_chunk;
    while(1)
    {
        //inuse Chunk
	if(inuse(chunk_ptr)){
	    printf("Node Address : %p" , chunk2mem(chunk_ptr));
	    pos_check_unsafe_region(name, av, head, chunk_ptr);
	    printf("\n");
	}
	if(chunk_is_last(chunk_ptr))
	    break;

	chunk_ptr = next_chunk(chunk_ptr);		
    }
}

// pointer detection
Void_t* lookup_pointer(mchunkptr p , unsigned long offset)
{
    Void_t *node;	
	
    node = chunk2mem(p);
    return *(void **)(node + offset);	
}

// Persistent Region address check
int check_valid_address(struct seg_info *head, Void_t *ptr)
{
    int i = 0;

    while(head[i].addr != 0)
    {
        if((ptr >= (void *)head[i].addr) && (ptr < (void *)(head[i].addr + head[i].size)))
            return 1;
        i++;
    }
    return 0;
}

// Chunk_Check 
void pos_check_unsafe_region(char *name, mstate av, struct seg_info *head, mchunkptr chunk_ptr)
{
    int i;
    Void_t *ptr;
    Void_t *new_ptr;
	
    for(i = 0; i < 50; i++)
    {
        if(av -> node_obj.ptr_offset[i] == 0)
	    break;

	ptr = lookup_pointer(chunk_ptr, av->node_obj.ptr_offset[i]);
	if((!check_valid_address(head, ptr)) && ptr != NULL)
	{	
	    new_ptr = pos_unsafe_region_relocate(name, av, ptr);
	    chunk_change_pointer(chunk_ptr, new_ptr, av->node_obj.ptr_offset[i]);
	}		
    }	
}

// Change Pointer
void chunk_change_pointer(mchunkptr chunk_ptr, Void_t *p , unsigned long offset)
{
    Void_t *mem_ptr;

    mem_ptr = chunk2mem(chunk_ptr) + offset;
    memcpy(mem_ptr, &p, sizeof(Void_t *));
}


// unsafe(Volatile) -> safe(Non-Volatile)
Void_t* pos_unsafe_region_relocate(char *name, mstate av, Void_t *p)
{
    mchunkptr chunk_ptr;
    mchunkptr next_chunk_ptr;
    Void_t *new_addr;
    Void_t *mem_ptr;
    Void_t *next_mem_ptr;
    int i,j;

    chunk_ptr = mem2chunk(p);
    new_addr = pos_malloc(name , av -> node_obj.size);
    memcpy(new_addr , p , av -> node_obj.size);	    
    printf(" --->     NodePtr : %p" , p);
    printf(" --->     NewPtr : %p" , new_addr);

    for(i = 0; i < 50; i++)
    {
        if(av->node_obj.ptr_offset[i] == 0)
	    break;

	mem_ptr = lookup_pointer(mem2chunk(new_addr), av->node_obj.ptr_offset[i]);
	if(mem_ptr == NULL)
	    break;

	for(j = 0; j < 50; j++)
	{
	    if(av->node_obj.ptr_offset[j] == 0)
	        break;

	    next_mem_ptr = lookup_pointer(mem2chunk(mem_ptr), av->node_obj.ptr_offset[j]);
	    if(next_mem_ptr == NULL)
  	        break;

	    if(next_mem_ptr == p){
	        chunk_change_pointer(mem2chunk(mem_ptr), new_addr, av->node_obj.ptr_offset[j]);   
		}
	}	
    }
    return new_addr;
}






