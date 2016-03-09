/*
 * Created by Ivo Georgiev on 2/9/16.
 */

#include <stdlib.h>
#include <assert.h>
#include <stdio.h> // for perror()

#include "mem_pool.h"

/*************/
/*           */
/* Constants */
/*           */
/*************/
static const float      MEM_FILL_FACTOR                 = 0.75;
static const unsigned   MEM_EXPAND_FACTOR               = 2;

static const unsigned   MEM_POOL_STORE_INIT_CAPACITY    = 20;
static const float      MEM_POOL_STORE_FILL_FACTOR      = 0.75;
static const unsigned   MEM_POOL_STORE_EXPAND_FACTOR    = 2;

static const unsigned   MEM_NODE_HEAP_INIT_CAPACITY     = 40;
static const float      MEM_NODE_HEAP_FILL_FACTOR       = 0.75;
static const unsigned   MEM_NODE_HEAP_EXPAND_FACTOR     = 2;

static const unsigned   MEM_GAP_IX_INIT_CAPACITY        = 40;
static const float      MEM_GAP_IX_FILL_FACTOR          = 0.75;
static const unsigned   MEM_GAP_IX_EXPAND_FACTOR        = 2;



/*********************/
/*                   */
/* Type declarations */
/*                   */
/*********************/
typedef struct _node {
    alloc_t alloc_record;
    unsigned used;
    unsigned allocated;
    struct _node *next, *prev; // doubly-linked list for gap deletion
} node_t, *node_pt;

typedef struct _gap {
    size_t size;
    node_pt node;
} gap_t, *gap_pt;

typedef struct _pool_mgr {
    pool_t pool;
    node_pt node_heap;
    unsigned total_nodes;
    unsigned used_nodes;
    gap_pt gap_ix;
    unsigned gap_ix_capacity;
} pool_mgr_t, *pool_mgr_pt;


//Used to Print Individual Nodes in Node Heap
void printNode(char name[], node_t node){
    printf("\n%s: { \n  Address: %p\n", name, &node);
    printf("  Alloc Record: { \n    Size: %lu\n    Address: %p\n  }\n", node.alloc_record.size, node.alloc_record.mem);
    printf("  Allocated: %d\n", node.allocated);
    printf("  Used: %d\n", node.used);
    printf("  Next Address: %p\n", node.next);
    printf("  Previous Address: %p\n}\n", node.prev);
}

//Used to Print Individual Pools
void printPool(char name[], pool_t pool){
    printf("\n%s: { \n  Memory:%p\n", name, pool.mem);
    printf("  Policy: %d\n", pool.policy);
    printf("  Size: %lu\n", pool.total_size);
    printf("  Alloc Size: %lu\n", pool.alloc_size);
    printf("  Num Allocs: %d\n", pool.num_allocs);
    printf("  Num Gaps: %d\n", pool.num_gaps);
    printf("}\n");
}

//Used to Print Individual Gaps
void printGap(char name[], gap_t gap){
    printf("\n%s: { \n  Size:%lu\n", name, gap.size);
    printf("  Node: { \n    Address: %p\n", &gap.node);
    printf("    Alloc Record: { \n      Size: %lu\n      Address: %p\n    }\n", gap.node->alloc_record.size, gap.node->alloc_record.mem);
    printf("    Allocated: %d\n", gap.node->allocated);
    printf("    Used: %d\n", gap.node->used);
    printf("    Next Address: %p\n", gap.node->next);
    printf("    Previous Address: %p\n  }\n}", gap.node->prev);
}

void printNodes(pool_mgr_pt mgr){
    int count = 0;
    node_pt node;
    node = mgr->node_heap;
    printNode("Node1", *node);
    while(node->next != NULL){
        node = node->next;
        printNode("NextNode", *node);
    }
}

void printGaps(pool_mgr_pt mgr){
    for(int i = 0; i < mgr->pool.num_gaps; i++){
        printGap("Gap", mgr->gap_ix[i]);
    }
}

/***************************/
/*                         */
/* Static global variables */
/*                         */
/***************************/
static pool_mgr_pt *pool_store = NULL; // an array of pointers, only expand
static unsigned pool_store_size = 0;
static unsigned pool_store_capacity = 0;



/********************************************/
/*                                          */
/* Forward declarations of static functions */
/*                                          */
/********************************************/
static alloc_status _mem_resize_pool_store();
static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr);
static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr);
static alloc_status
        _mem_add_to_gap_ix(pool_mgr_pt pool_mgr,
                           size_t size,
                           node_pt node);
static alloc_status
        _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr,
                                size_t size,
                                node_pt node);
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr);



/****************************************/
/*                                      */
/* Definitions of user-facing functions */
/*                                      */
/****************************************/
alloc_status mem_init() {
    // ensure that it's called only once until mem_free
    // allocate the pool store with initial capacity
    // note: holds pointers only, other functions to allocate/deallocate
    if(pool_store != NULL){
        return ALLOC_CALLED_AGAIN;
    }
    pool_store_capacity = 10;
    pool_store = calloc(MEM_POOL_STORE_INIT_CAPACITY, sizeof(pool_mgr_pt));
    return ALLOC_OK;

}

alloc_status mem_free() {
    // ensure that it's called only once for each mem_init
    // make sure all pool managers have been deallocated
    // can free the pool store array
    // update static variables

    return ALLOC_FAIL;
}

pool_pt mem_pool_open(size_t size, alloc_policy policy) {
    // make sure there the pool store is allocated
    if(pool_store == NULL){
        return NULL;
    }
    //expand the pool store, if necessary
    _mem_resize_pool_store();

    // allocate a new mem pool mgr
    pool_mgr_pt pool_mgr = calloc(1, sizeof(pool_mgr_t));
    // check success, on error return null
    if(pool_mgr == NULL){
        return NULL;
    }
    // allocate a new memory pool
    pool_mgr->pool.mem = malloc(size);
    pool_mgr->pool.policy = policy;
    pool_mgr->pool.total_size = size;
    pool_mgr->pool.alloc_size = 0;
    pool_mgr->pool.num_allocs = 0;
    pool_mgr->pool.num_gaps = 1;
    // check success, on error deallocate mgr and return null
    if(pool_mgr->pool.mem == NULL){
        free(pool_mgr);
        return NULL;
    }
    // allocate a new node heap
    pool_mgr->node_heap = malloc(sizeof(node_t));
    // check success, on error deallocate mgr/pool and return null
    if(pool_mgr->node_heap == NULL){
        free(pool_mgr);
        free(pool_mgr->pool.mem);
        return NULL;
    }
    // allocate a new gap index
    pool_mgr->gap_ix = calloc(MEM_GAP_IX_INIT_CAPACITY, sizeof(gap_t));
    // check success, on error deallocate mgr/pool/heap and return null
    if(pool_mgr->gap_ix == NULL){
        free(pool_mgr);
        free(pool_mgr->pool.mem);
        free(pool_mgr->node_heap);
        return NULL;
    }
    // assign all the pointers and update meta data:
    //   initialize top node of node heap
    pool_mgr->node_heap[0].alloc_record.mem = pool_mgr->pool.mem;
    pool_mgr->node_heap[0].alloc_record.size = size;
    pool_mgr->node_heap[0].used = 0;
    pool_mgr->node_heap[0].allocated = 0;
    pool_mgr->node_heap->next = NULL;
    pool_mgr->node_heap->prev = NULL;


    //   initialize top node of gap index
    pool_mgr->gap_ix[0].node = &pool_mgr->node_heap[0];
    pool_mgr->gap_ix[0].size = size;
    //   link pool mgr to pool store
    pool_store[pool_store_size] = pool_mgr;
    pool_store_size++;
    // return the address of the mgr, cast to (pool_pt)

    pool_mgr->total_nodes++;

    return (pool_pt) pool_mgr;
}

alloc_status mem_pool_close(pool_pt pool) {
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    //pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;
    // check if this pool is allocated
    // check if pool has only one gap
    // check if it has zero allocations
    // free memory pool
    // free node heap
    // free gap index
    // find mgr in pool store and set to null
    // note: don't decrement pool_store_size, because it only grows
    // free mgr

    return ALLOC_FAIL;
}

alloc_pt mem_new_alloc(pool_pt pool, size_t size) {
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;
    // check if any gaps, return null if none
    if(pool_mgr->pool.num_gaps == 0){
        return NULL;
    }
    // expand heap node, if necessary, quit on error
    _mem_resize_node_heap(pool_mgr);

    // check used nodes fewer than total nodes, quit on error TODO

    // get a node for allocation:
    node_pt selected_node = malloc(sizeof(node_t));
    int found = 0;
    // if FIRST_FIT, then find the first sufficient node in the node heap
    if(pool->policy == FIRST_FIT){
        int complete = 0;
        selected_node = pool_mgr->node_heap;
        do {
            if (selected_node->used == 0) {
                if(selected_node->allocated == 0){
                    found = 1;
                    complete = 1;
                } else{
                    if(selected_node->alloc_record.size >= size){
                        found = 1;
                        complete = 1;
                    } else{
                        selected_node = selected_node->next;
                    }
                }
            }
            else{
                if(selected_node->next != NULL){
                    selected_node = selected_node->next;
                }else{
                    complete = 1;
                }
            }
        }while(!complete);
    }
    else{
        for(int i = 0; i < pool_mgr->pool.num_gaps; i++){
            if(pool_mgr->gap_ix[i].size >= size){
                if(!found){
                    found = 1;
                    selected_node = pool_mgr->gap_ix[i].node;
                }
            }
        }
    }
    // check if node found
    if(!found){
        return NULL;
    }
    // update metadata (num_allocs, alloc_size)
    pool_mgr->pool.num_allocs++;
    pool_mgr->pool.alloc_size += size;
    // calculate the size of the remaining gap, if any
    size_t remainder = selected_node->alloc_record.size - size;
    // remove node from gap index
    _mem_remove_from_gap_ix(pool_mgr, size, selected_node);
    // convert gap_node to an allocation node of given size
    selected_node->allocated = 1;
    selected_node->used = 1;
    selected_node->alloc_record.size = size;

    // adjust node heap:
    //   if remaining gap, need a new node
    //   find an unused one in the node heap
    //   make sure one was found
    //   initialize it to a gap node
    if(remainder > 0){
        node_pt node = malloc(sizeof(node_t));
        node->alloc_record.size = remainder;
        node->alloc_record.mem = selected_node->alloc_record.mem + size;
        node->used = 0;
        node->allocated = 0;
        node->next = NULL;
        node->prev = selected_node;
        if(selected_node->next != NULL){
            node->next = selected_node->next;
        }
        //   update linked list (new node right after the node for allocation)
        selected_node->next = node;
        //   add to gap index
        _mem_add_to_gap_ix(pool_mgr, node->alloc_record.size, node);
        //   check if successful

    }

    //   update metadata (used_nodes)
    pool_mgr->used_nodes++;
    pool_mgr->total_nodes++;

    printNodes(pool_mgr);
    // return allocation record by casting the node to (alloc_pt)
    return &selected_node->alloc_record;
}

alloc_status mem_del_alloc(pool_pt pool, alloc_pt alloc) {
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;
    // get node from alloc by casting the pointer to (node_pt)
    node_pt node = (node_pt) alloc;
    // find the node in the node heap
    // this is node-to-delete
    // make sure it's found
    if(node == NULL){
        return ALLOC_FAIL;
    }
    // convert to gap node
    // update metadata (num_allocs, alloc_size)
    pool_mgr->pool.num_allocs--;
    pool_mgr->pool.alloc_size = pool_mgr->pool.alloc_size - node->alloc_record.size;
    // if the next node in the list is also a gap, merge into node-to-delete
    node_pt next = node->next;
    if(next->used == 0) {
        //   add the size to the node-to-delete
        node->alloc_record.size += next->alloc_record.size;
        //   remove the next node from gap index

    }
        //   update node as unused
        node->used = 0;
    //   check success
    //   update metadata (used nodes)
    //   update linked list:
    /*
                    if (next->next) {
                        next->next->prev = node_to_del;
                        node_to_del->next = next->next;
                    } else {
                        node_to_del->next = NULL;
                    }
                    next->next = NULL;
                    next->prev = NULL;
     */

    // this merged node-to-delete might need to be added to the gap index
    // but one more thing to check...
    // if the previous node in the list is also a gap, merge into previous!
    //   remove the previous node from gap index
    //   check success
    //   add the size of node-to-delete to the previous
    //   update node-to-delete as unused
    //   update metadata (used_nodes)
    //   update linked list
    /*
                    if (node_to_del->next) {
                        prev->next = node_to_del->next;
                        node_to_del->next->prev = prev;
                    } else {
                        prev->next = NULL;
                    }
                    node_to_del->next = NULL;
                    node_to_del->prev = NULL;
     */
    //   change the node to add to the previous node!
    // add the resulting node to the gap index
    // check success

    return ALLOC_FAIL;
}

void mem_inspect_pool(pool_pt pool,
                      pool_segment_pt *segments,
                      unsigned *num_segments) {
    // get the mgr from the pool
    pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;

    // allocate the segments array with size == used_nodes
    // check successful
    pool_segment_pt segs;
    segs = calloc(pool_mgr->total_nodes, sizeof(pool_segment_t));

    if(segments == NULL){
        return;
    }
    // loop through the node heap and the segments array
    //    for each node, write the size and allocated in the segment
    // "return" the values:
    node_pt current = pool_mgr->node_heap;
    int i = 0;
    do{
        segs[i].allocated = current->allocated;
        segs[i].size = current->alloc_record.size;
        current = current->next;
        i++;
    } while(current->next != NULL);
    segs[i].allocated = current->allocated;
    segs[i].size = current->alloc_record.size;

    *segments = segs;
    *num_segments = pool_mgr->total_nodes;
    return;


}



/***********************************/
/*                                 */
/* Definitions of static functions */
/*                                 */
/***********************************/
static alloc_status _mem_resize_pool_store() {
    // check if necessary
    /*
                if (((float) pool_store_size / pool_store_capacity)
                    > MEM_POOL_STORE_FILL_FACTOR) {...}
     */
    // don't forget to update capacity variables

    return ALLOC_FAIL;
}

static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr) {
    // see above

    return ALLOC_FAIL;
}

static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr) {
    // see above

    return ALLOC_FAIL;
}

static alloc_status _mem_add_to_gap_ix(pool_mgr_pt pool_mgr,
                                       size_t size,
                                       node_pt node) {


    // expand the gap index, if necessary (call the function)
    _mem_resize_gap_ix(pool_mgr);
    // add the entry at the end

    pool_mgr->gap_ix[pool_mgr->pool.num_gaps].node = node;
    pool_mgr->gap_ix[pool_mgr->pool.num_gaps].size = size;
    // update metadata (num_gaps)
    pool_mgr->pool.num_gaps++;
    // sort the gap index (call the function)
    // check success
    if(_mem_sort_gap_ix(pool_mgr) == ALLOC_OK){
        return ALLOC_OK;
    };

    return ALLOC_FAIL;
}

static alloc_status _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr,
                                            size_t size,
                                            node_pt node) {
    int position = -1;
   // printNode("Looking at", *node);
    for(int i = 0; i < pool_mgr->pool.num_gaps; i++){
        // find the position of the node in the gap index
        if(node->alloc_record.mem == pool_mgr->gap_ix[i].node->alloc_record.mem){
            position = i;
        }
    }

    if(position == -1){
        printf("Didn't find NODE");
        return ALLOC_FAIL;
    }


    // loop from there to the end of the array
    //    pull the entries (i.e. copy over) one position up
    for(int i = position; i < pool_mgr->pool.num_gaps-1; i++){
        pool_mgr->gap_ix[i] = pool_mgr->gap_ix[i+1];
    }

    // zero out the element at position num_gaps!
    pool_mgr->gap_ix[pool_mgr->pool.num_gaps-1].size = 0;
    pool_mgr->gap_ix[pool_mgr->pool.num_gaps-1].node->alloc_record.size = 0;
    pool_mgr->gap_ix[pool_mgr->pool.num_gaps-1].node->alloc_record.mem = 0;
    pool_mgr->gap_ix[pool_mgr->pool.num_gaps-1].node->allocated = 0;
    pool_mgr->gap_ix[pool_mgr->pool.num_gaps-1].node->used = 0;
    pool_mgr->gap_ix[pool_mgr->pool.num_gaps-1].node->next = NULL;
    pool_mgr->gap_ix[pool_mgr->pool.num_gaps-1].node->prev = NULL;

    // update metadata (num_gaps)
    pool_mgr->pool.num_gaps--;

    return ALLOC_OK;
}

// note: only called by _mem_add_to_gap_ix, which appends a single entry
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr) {
    // the new entry is at the end, so "bubble it up"
    // loop from num_gaps - 1 until but not including 0:

    //    if the size of the current entry is less than the previous (u - 1)
    //       swap them (by copying) (remember to use a temporary variable)

    return ALLOC_FAIL;
}
