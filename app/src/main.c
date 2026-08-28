#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <inttypes.h>

LOG_MODULE_REGISTER(l2_task1, LOG_LEVEL_DBG);

#define STACK_SIZE 1024

#define PRIO_A 5
#define PRIO_B 5
#define MAX_ITER 500000ULL

//#define USE_MUTEX 1

static volatile uint64_t shared_counter;
#ifdef USE_MUTEX
static struct k_mutex counter_mutex;
#endif

void thread_a_fn(void *p1, void *p2, void *p3)
{
    LOG_INF("T_A started counting.");
    for (uint64_t i = 0; i < MAX_ITER; ++i) {
#ifdef USE_MUTEX
        k_mutex_lock(&counter_mutex, K_FOREVER);
#endif
        shared_counter++;
#ifdef USE_MUTEX
        k_mutex_unlock(&counter_mutex);
#endif
    }
    LOG_INF("T_A done counting.");
}

void thread_b_fn(void *p1, void *p2, void *p3)
{
    LOG_INF("T_B started counting.");
    for (uint64_t i = 0; i < MAX_ITER; ++i) {
#ifdef USE_MUTEX
        k_mutex_lock(&counter_mutex, K_FOREVER);
#endif
        shared_counter++;
#ifdef USE_MUTEX
        k_mutex_unlock(&counter_mutex);
#endif
    }
    LOG_INF("T_B done counting.");
}

static struct k_thread thread_a_data;
static struct k_thread thread_b_data;
static K_THREAD_STACK_DEFINE(thread_a_stack, STACK_SIZE);
static K_THREAD_STACK_DEFINE(thread_b_stack, STACK_SIZE);

int main(void)
{
#ifdef USE_MUTEX
    k_mutex_init(&counter_mutex);
#endif
    LOG_INF("Race start: counter=%" PRIu64, shared_counter);
    k_thread_create(&thread_a_data, thread_a_stack, STACK_SIZE,
                    thread_a_fn, NULL, NULL, NULL, PRIO_A, 0, K_MSEC(500));
    k_thread_create(&thread_b_data, thread_b_stack, STACK_SIZE,
                    thread_b_fn, NULL, NULL, NULL, PRIO_B, 0, K_MSEC(500));
    k_thread_join(&thread_a_data, K_FOREVER);
    k_thread_join(&thread_b_data, K_FOREVER);
    LOG_INF("Race end: counter=%" PRIu64 " (expected %" PRIu64 ")",
             shared_counter, MAX_ITER * 2);
    return 0;
}
