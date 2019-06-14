/*
 * Copyright 2019, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */
#include <stdlib.h>
#include <stdint.h>
#include <clock/clock.h>
#include <adt/dynamic.h>
#include <adt/priority_q.h>

/* The functions in src/device.h should help you interact with the timer
 * to set registers and configure timeouts. */
#include "device.h"

#define TICK_ADVANCE 10
#define CALLBACK_ADVANCE 900
#define TOTAL_ADVANCE (TICK_ADVANCE + CALLBACK_ADVANCE)


struct Timer_s
{
    size_t id;
    uint64_t end_stamp;
    timer_callback_t callback;
    void * data; 
    // if someone want to disable some clock
    // this is the word 
    bool enabled;
};
typedef struct Timer_s * Timer_t;
static struct {
    volatile meson_timer_reg_t *regs;
    /* Add fields as you see necessary */
    size_t curr_timer;
    size_t overflow_timer;
    // two management structure
    DynamicArr_t timer_arr; 
    PriorityQueue_t timer_pq_curr; 
    PriorityQueue_t timer_pq_next;
} clock;
// a simple define function
#define currTimestamp read_timestamp(clock.regs)

/**
 * Helper function to get and set up current timer 
 */
static inline Timer_t get_curr_timer(){
    if (clock.curr_timer == 0) return NULL;
    return DynamicArr__get(clock.timer_arr, clock.curr_timer - 1);
}

static inline void set_curr_timer(size_t id){
    clock.curr_timer = id;
}


int timer_compare(void* index_a, void* index_b){
    // id as pointer 
    Timer_t a = DynamicArr__get(clock.timer_arr, ((size_t) index_a)-1);
    Timer_t b = DynamicArr__get(clock.timer_arr, ((size_t) index_b)-1);
    // the smaller is more important 
    if(a->end_stamp < b->end_stamp){
        return 1;
    }
    else if(a->end_stamp > b->end_stamp){
        return -1;
    }
    return 0;
}

static inline Timer_t get_timer_by_id (size_t id){
    if (id != 0){
        return DynamicArr__get(clock.timer_arr, id - 1);
    }
    // ELSE
    return NULL;
}

static inline Timer_t find_new_curr_timer(){
    return get_timer_by_id((size_t) PriorityQueue__first(clock.timer_pq_curr));
}

static inline size_t add_timer_helper(Timer_t timer){
    /**
     * Helper function, alloc and set up timer id
     * @timer: pointer to the setted up timer 
     * @ret: id for the alloced timer in array
     */
    size_t alloc_timer_id = DynamicArr__add(clock.timer_arr,timer) + 1 ;
    get_timer_by_id(alloc_timer_id) ->id = alloc_timer_id;
    return alloc_timer_id;
}

void timer_cleanup(Timer_t timer){
    /**
     * callback those enabled, discard those is not
     * @timer: the timer need to clean up 
     * @pre: timer has been deleted in priority queue first
     * @post: timer deleted in dynamic array
     */
    if (timer->enabled)
    {
        timer->callback(timer->id,timer->data);
    }
    timer->enabled = false;
    // remove this is small alloc 
    DynamicArr__del(clock.timer_arr,timer->id -1);
}

/**
 * To Prevent 64-bit overflow
 */
void switch_queue(uint32_t id, void * data){
    // switch the queue 
    // count over callback
    // althought we can trust there's nothing in curr, 
    // it's better to wake it all up 
    for (
        Timer_t in_curr_timer = get_timer_by_id(
            (size_t) PriorityQueue__pop(clock.timer_pq_curr)
        );
        in_curr_timer != NULL; 
        in_curr_timer = get_timer_by_id(
            (size_t) PriorityQueue__pop(clock.timer_pq_curr)
        )
    ){
        timer_cleanup(in_curr_timer);
    }
    
    // actual switch the queue
    PriorityQueue_t temp = clock.timer_pq_curr;
    clock.timer_pq_curr  = clock.timer_pq_next;
    clock.timer_pq_next  = temp;
}

void set_overflow_timer(){
    /* to prevent 64-bit overflow */

    // overflow timer has been setted
    if (clock.overflow_timer != 0) return;

    struct Timer_s a_timer;
    // we use us to better calculation, but our resolution is 10 us 
    a_timer.end_stamp = MASK(64);
    a_timer.data      = NULL;
    a_timer.callback  = switch_queue;
    a_timer.enabled   = true;
    // alloock in the dynamic array
    clock.overflow_timer = add_timer_helper(&a_timer);
    // always add to the current queue 
    PriorityQueue__add(clock.timer_pq_curr, (void *) clock.overflow_timer);
}

int start_timer(void *timer_vaddr)
{
    // printf("Try to init timer\n");
    if ((meson_timer_reg_t *)(timer_vaddr + TIMER_REG_START) == clock.regs){
        // these timer has been inited 
        int err = stop_timer();
        if (err != 0) {
            return err;
        }
    }
    

    clock.regs = (meson_timer_reg_t *)(timer_vaddr + TIMER_REG_START);

    // clean up the physical settings 
    // we will use  timer a first
    configure_timeout(clock.regs, MESON_TIMER_A, true, false, TIMEOUT_TIMEBASE_10_US, 0);
    // set the timestamp 
    configure_timestamp(clock.regs, TIMESTAMP_TIMEBASE_1_US);

    /* intialise management structure */
    // printf("Try to init timer\n");

    clock.timer_arr      = DynamicArr__init(sizeof(struct Timer_s));
    // printf("init dynamic arr succ\n");
    clock.timer_pq_curr  = PriorityQueue__init(timer_compare);
    // printf("init pq curr\n");

    clock.timer_pq_next  = PriorityQueue__init(timer_compare);
    // printf("init pq next\n");

    // 0 as the null id, not occupied
    clock.curr_timer     = 0;
    clock.overflow_timer = 0;

    return CLOCK_R_OK;
}

timestamp_t get_time(){
    // return currTimestamp ;
    return currTimestamp / 1000;
}


void try_set_timer(){
    Timer_t suggest_timer = find_new_curr_timer();
    for (;
        // there need a total_advance to call the callback
        // probably there need anther advance to set it up 
        suggest_timer != NULL && 
        (
            suggest_timer->end_stamp <= currTimestamp + TOTAL_ADVANCE||
            suggest_timer->enabled  == false
        );
        suggest_timer = find_new_curr_timer()
    ){
        // because the pop is the only way we can delete an element in pq
        // so we use permitive to delete here
        timer_cleanup(
            get_timer_by_id(
                (size_t) PriorityQueue__pop(clock.timer_pq_curr)
            )
        );
    }

    /* no more timer to set */
    if (suggest_timer == NULL) return;
    // printf("Compare: %lu, %lu\n",suggest_timer->end_stamp,currTimestamp - TOTAL_ADVANCE);


    uint64_t gap = (suggest_timer-> end_stamp - TICK_ADVANCE - currTimestamp )/10;
    // printf("suggest %lu, write: %lu\n", suggest_timer->id , gap);

    // Too much I can count 
    // so write the max I can count 
    if (gap> MASK(16)) gap = MASK(16);  

    // printf("write a timeout %lu\n",);
    write_timeout(
        clock.regs,
        MESON_TIMER_A,
        gap
    );

    // release the lock 
    // i can put the correct timer link to this suggest timer 
    clock.curr_timer = suggest_timer -> id;
}

uint32_t register_timer(uint64_t delay, timer_callback_t callback, void *data){
    // identify whether it's fake timer 
    if (delay <= TOTAL_ADVANCE)
    {
        // this timer is quicker to directly calling the callbacks 
        // and it's fake timer, so it return 0
        callback(0, data);
        return 0;
    }

    struct Timer_s a_timer;
    // we use us to better calculation, but our resolution is 10 us 
    a_timer.end_stamp = currTimestamp + delay;
    a_timer.data      = data;
    a_timer.callback  = callback;
    a_timer.enabled   = true;
    // add the things into ADT, and find the timer and decide which queue it goes to 
    size_t id = add_timer_helper(&a_timer);
    
    // decide which pq it will go into 
    if (a_timer.end_stamp < currTimestamp)
    {
        /* this must be too big to count in this cycle */
        // enable prevent 64-bit overflow 
        set_overflow_timer();
        // here may be a good place to add a switch queue function
        PriorityQueue__add(clock.timer_pq_next, (void *)id);
    } else {
        PriorityQueue__add(clock.timer_pq_curr, (void *)id);
    }
    
    try_set_timer();

    // printf("%lu\twant to end at %lu\n", id,get_timer_by_id(id)->end_stamp);

    return id;
}

int remove_timer(uint32_t id)
{
    Timer_t del_timer = get_timer_by_id(id);
    // disable it in the datastructure;
    del_timer->enabled = false;

    // I have to try to set up a new timer 
    if (id == clock.curr_timer) try_set_timer();        

    return 0;
}

int timer_irq(
    void *data,
    seL4_Word irq,
    seL4_IRQHandler irq_handler
){
    /* Acknowledge that the IRQ has been handled */
    seL4_IRQHandler_Ack(irq_handler);
    /* Handle the IRQ */
    uint64_t timer_id = (uint64_t) data;
    switch (timer_id)
    {
    case MESON_TIMER_A:
        if(
            get_timer_by_id(clock.curr_timer)->end_stamp 
            <=
            currTimestamp + TICK_ADVANCE
        ){
            // fast path
            Timer_t curr= get_curr_timer();
            curr->callback(curr->id,curr->data);
            curr->enabled = false;

            // this may take some times 
            PriorityQueue__pop(clock.timer_pq_curr);
        }
        break;
    }

    try_set_timer();

    return seL4_NoError;
}

int stop_timer(void)
{
    /* Stop the timer from producing further interrupts and remove all
     * existing timeouts */
    // stop the interupt 
    configure_timeout(clock.regs, MESON_TIMER_A, false, false, TIMEOUT_TIMEBASE_10_US, 0);
    // free those datastructure 
    DynamicArr__free(clock.timer_arr);
    PriorityQueue__free(clock.timer_pq_curr);
    PriorityQueue__free(clock.timer_pq_next);

    return 0;
}
