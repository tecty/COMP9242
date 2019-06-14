#if !defined(CLOCK_TEST_H)
#define CLOCK_TEST_H

#include <clock/clock.h>
#include <clock/timestamp.h>
#include <serial/serial.h>

void register_test_callback(struct serial * s,void *timer_vaddr_in);

#endif // CLOCK_TEST_H
