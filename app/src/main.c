#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(l1_task1, LOG_LEVEL_DBG);

#define STACK_SIZE 1024

#define PRIO_LOW    7
#define PRIO_MED    5
#define PRIO_HIGH   3
#define PRIO_COOP   (-1)

void t_low_fn(void *p1, void *p2, void *p3)
{
    while (1) {
        LOG_INF("T_LOW running t=%u", k_uptime_get_32());
        k_msleep(300);
    }
}

void t_med_fn(void *p1, void *p2, void *p3)
{
    while (1) {
        LOG_INF("T_MED running t=%u", k_uptime_get_32());
        k_msleep(200);
    }
}

void t_high_fn(void *p1, void *p2, void *p3)
{
    while (1) {
        LOG_INF("T_HIGH running t=%u", k_uptime_get_32());
        k_msleep(100);
    }
}

void t_coop_fn(void *p1, void *p2, void *p3)
{
    for (int i = 0; i < 5; i++) {
        LOG_INF("T_COOP busy work %d/5 t=%u", i + 1, k_uptime_get_32());
        k_busy_wait(40000);
    }
    LOG_INF("T_COOP yielding t=%u", k_uptime_get_32());
    k_yield();
    LOG_INF("T_COOP done t=%u", k_uptime_get_32());
}

K_THREAD_DEFINE(t_low,  STACK_SIZE, t_low_fn,  NULL, NULL, NULL, PRIO_LOW,  0, 0);
K_THREAD_DEFINE(t_med,  STACK_SIZE, t_med_fn,  NULL, NULL, NULL, PRIO_MED,  0, 0);
K_THREAD_DEFINE(t_high, STACK_SIZE, t_high_fn, NULL, NULL, NULL, PRIO_HIGH, 0, 0);
K_THREAD_DEFINE(t_coop, STACK_SIZE, t_coop_fn, NULL, NULL, NULL, PRIO_COOP, 0, 0);

/* UART analysis (~8s capture):
 * T_HIGH: 78 runs  (priority 3, 100ms sleep) -> most often
 * T_MED:  39 runs  (priority 5, 200ms sleep)
 * T_LOW:  26 runs  (priority 7, 300ms sleep)
 * T_COOP: busy work then yields
 */
int main(void)
{
    LOG_INF("=== Ready vs Waiting / yield vs sleep === t=%u", k_uptime_get_32());
    return 0;
}
