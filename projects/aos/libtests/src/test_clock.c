#include <tests/clock.h>
#include <stdio.h>
#include <stdbool.h>

#define currTimestamp  timestamp_ms(freq)

bool in_test;
bool has_error;
uint64_t expected_timout[4096];
uint64_t freq;
int total_timer;
void *timer_vaddr;
typedef void (*test_callback_t)();

static inline uint64_t abs_64(uint64_t a, uint64_t b){
    if (a> b) return a-b;
    return b-a;
}

static inline void dump_timer(uint64_t end_at, uint32_t id){
    // printf(
    //     "%u-Now: %lu\tExp: %lu\t Var: %lu\n",
    //     id,end_at, expected_timout[id], abs_64(end_at, expected_timout[id])
    // );
}


void verify_timer(uint32_t id, void * data){
    uint64_t end_at = currTimestamp;
    total_timer --;

    /**
     * Error cases 
     */
    if (abs_64(end_at, expected_timout[id])  > (uint64_t) data)
    {
        dump_timer(end_at, id);
        has_error= true;
    }
    if (expected_timout[id] == -1)
    {
        printf("%u-This Timer shouldn't be triggered\n", id);
        dump_timer(end_at, id);
        has_error= true;
    }
    
    // this timer has touch, disable it 
    expected_timout[id] = -1;

    /**
     * Timer has verified
     */
    if(total_timer == 0){
        if (has_error ==false)
        {
            printf("Pass\n");
        } else {
            printf("Finish with some error.\n");
        }
        
        in_test = false;
        has_error = false;
    } else if (total_timer < 0)
    {
        printf("Some error occour");
    }
}


void test_10_timer(){
    printf("Reigster Timer in Order\n");
    total_timer = 10;
    for (size_t i = 1; i <= total_timer; i++){
        uint32_t id = (uint32_t) register_timer(133000*i, verify_timer, (void *)0);
        expected_timout[id] = currTimestamp + (133000*i/ 1000);
    }
    
}

void test_10_timer_rev(){
    printf("Reigster Timer in Reverse Order\n");
    total_timer = 10;
    for (size_t i = total_timer; i >=1; i--){
        uint32_t id = (uint32_t) register_timer(500000*i, verify_timer, (void *)0);
        expected_timout[id] = currTimestamp + (500000*i/ 1000);
    }
}

void test_10_collision_timer(){
    printf("Reigster Collision Timer \n");
    total_timer = 20;
    for (size_t i = total_timer; i >=1; i--){
        uint32_t id = (uint32_t) register_timer(1000000, verify_timer, (void *)10);
        expected_timout[id] = currTimestamp + (1000000/ 1000);
    }
}

void test_remove_timer(){
    printf("Remove timer\n");
    total_timer = 10;
    uint64_t ids[10];
    for (size_t i = 0; i < total_timer; i++)
    {
        uint32_t id = (uint32_t) register_timer(2000000, verify_timer, (void *)0);
        expected_timout[id] = currTimestamp + (2000000/ 1000);
        ids[i] = id;
    }
    
    /* Delet e the timer which id is double */
    for (size_t i = 0; i < total_timer; i++)
    {
        if (ids[i]%2 ==0)
        {
            remove_timer(ids[i]);
            total_timer --;
            expected_timout[ids[i]] = (-1);
        }
    }
}

void test_64_bit_overflow(){
    printf("64 Bit-overflow\n");

    total_timer = 1;
    // this timer couldn't be triggered 
    uint32_t id = (uint32_t) register_timer((-1), verify_timer, (void *)0);
    expected_timout[id] = (-1);
    id = (uint32_t) register_timer(10000000, verify_timer, (void *)0);
    expected_timout[id] = currTimestamp + (10000000/ 1000);
}

void test_unimplement(){
    printf("Not Implemented\n");
    in_test = false;
}

void verify_20_ms(uint32_t id, void * data){
    total_timer --;
    uint64_t old_stamp = (uint64_t) data;
    if(get_time() - old_stamp == 20){
        printf("Pass\n");
    } else{
        printf("Expected %lu\tBut %lu\n", old_stamp + 20, get_time());
    }
    in_test = false;
}

void test_timestamp_ms(){
    printf("Timestamp is ms\n");
    total_timer = 1;
    (uint32_t) register_timer(20000, verify_20_ms, (void *)get_time());
}

void test_restart_timer(){
    printf("Restart timer\n");
    total_timer = 10;
    for (size_t i = 0; i < 20; i++)
    {
        uint32_t id = (uint32_t) register_timer(2000000, verify_timer, (void *)0);
        expected_timout[id] = (-1);
    }
    start_timer(timer_vaddr);
    for (size_t i = 0; i < total_timer; i++)
    {
        uint32_t id = (uint32_t) register_timer(2000000, verify_timer, (void *)0);
        expected_timout[id] = currTimestamp + (2000000/ 1000);
    }
}

void test_lots_timer(){
    printf("Reigster A Lots of Timer\n");
    total_timer = 3000;
    for (size_t i = 1; i <= total_timer; i++){
        uint32_t id = (uint32_t) register_timer(10000*i, verify_timer, (void *)2);
        expected_timout[id] = currTimestamp + (10000*i/ 1000);
    }
}

void register100ms(uint32_t id, void * data){
    printf("%u Now is %lu\n",id ,get_time());
    total_timer --;
    uint64_t end_at = currTimestamp;


    if (end_at != expected_timout[id])
    {
        dump_timer(end_at,id);
    }
    
    if (total_timer > 0)
    {
        id = register_timer(100000, register100ms, (void *)0);
        // printf("registerd %u\n", id );

        expected_timout[id] = currTimestamp + 100;
    }
}

void test_regular_timer(){
    printf("Reigster 100 ms regular\n");
    in_test =false;
    total_timer += 20;
    size_t id =  register_timer(100000, register100ms, (void *)0);
    printf("registerd %lu\n", id );
    expected_timout[id] = currTimestamp + 100;
}

void test_big_small_timer(){
    printf("Register Big small timer with upto 50ms\n");
    total_timer = 30;
    for (size_t i = 1; i <= total_timer/3; i++){
        uint32_t id = (uint32_t) register_timer(10000*i, verify_timer, (void *)2);
        expected_timout[id] = currTimestamp + (10000*i/ 1000);
        id = (uint32_t) register_timer(10000*i, verify_timer, (void *)2);
        expected_timout[id] = currTimestamp + (10000*i/ 1000);
        id = (uint32_t) register_timer(50000*i, verify_timer, (void *)2);
        expected_timout[id] = currTimestamp + (50000*i/ 1000);
    }
}


// register all the callbcak 
test_callback_t test_arr[] = {
    test_10_timer,
    test_10_timer_rev,
    test_10_collision_timer,
    test_remove_timer,
    test_64_bit_overflow,
    test_timestamp_ms,
    test_restart_timer,
    test_lots_timer,
    test_regular_timer,
    test_big_small_timer,
};


void test_initiate(struct serial * s, char c ){
    if (!in_test && c >= '0' && c <= '9' ){
        in_test = true;
        c -= '0';
        printf("Test %d-",c);
        // call the callback to do the tests 
        test_arr[c]();
    }
}

void register_test_callback(struct serial * s ,void *timer_vaddr_in){
    total_timer = 0;
    in_test = false;
    has_error = false;
    timer_vaddr = timer_vaddr_in;
    freq = timestamp_get_freq();
    serial_register_handler(s, test_initiate);
}
