#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/random/random.h>
#include <zephyr/task_wdt/task_wdt.h>

LOG_MODULE_REGISTER(l5_task1, LOG_LEVEL_DBG);

#define STACK_SIZE 2048
#define MSG_COUNT 30
#define MAX_TRIES 3
#define SENSOR_PERIOD 200
#define HEALTHCHECK_PERIOD 1000
#define MAX_HEALTHCHECKS 254
#define SUB_MAX_QUEUE 12
#define CONSUMER_DELAY 1000
#define TERM_SEQ 0xFF /* End of production indicator */

struct sensor_data {
    int32_t temperature_mc;
    uint32_t timestamp_ms;
    uint8_t seq;
};

int format_temp(char *buf, size_t sz, int32_t temp_mc) {
    int deg_c = temp_mc / 1000;       // Get the whole number part
    temp_mc = (temp_mc < 0) ? -temp_mc : temp_mc;
    int frac_c = (temp_mc % 1000) / 100;    // Get the fractional part

    return snprintf(buf, sz, "%d.%d°C", deg_c, frac_c);
}

static void display_listener_cb(const struct zbus_channel *chan);

/* Listener — fast display update */
ZBUS_LISTENER_DEFINE(display_lis, display_listener_cb);

/* Message subscriber — receives message copies. */
ZBUS_SUBSCRIBER_DEFINE(logger_sub, SUB_MAX_QUEUE);

ZBUS_CHAN_DEFINE(sensor_chan, struct sensor_data,
                 NULL, NULL,
                 ZBUS_OBSERVERS(display_lis, logger_sub),
                 ZBUS_MSG_INIT(.temperature_mc = 0,
                               .timestamp_ms = 0,
                               .seq = 0));

/* ================================================================== */
/*  Listener callback                                                 */
/* ================================================================== */

static void display_listener_cb(const struct zbus_channel *chan)
{
    const struct sensor_data *msg =
        (const struct sensor_data *)zbus_chan_const_msg(chan);

	if (msg->seq == TERM_SEQ) {
            LOG_WRN("[DISPLAY-LIS] received end of production message");
			return;
	}

    char temp_str[10];
	format_temp(temp_str, sizeof(temp_str), msg->temperature_mc);

    LOG_INF("[DISPLAY-LIS] seq=%u temp=%s tick=%u",
            msg->seq, temp_str, k_uptime_get_32());
}

static int wdt_channel = -1;
static struct k_sem prod_done_sem;
static struct k_sem slow_down_prod_sem;

/* ================================================================== */
/*  Publisher thread                                                  */
/* ================================================================== */

static void sensor_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    k_thread_name_set(k_current_get(), "sensor");
	uint16_t period = SENSOR_PERIOD;

    for (uint8_t i = 0; i <= MSG_COUNT; ++i) {
        struct sensor_data data = {
            .temperature_mc = 24000 + (int32_t)(sys_rand32_get() % 5000),
            .timestamp_ms  = k_uptime_get_32(),
            .seq           = i,
        };

        char temp_str[10];
	    format_temp(temp_str, sizeof(temp_str), data.temperature_mc);

		/* Send terminator message */
		if (i == MSG_COUNT) {
			data.seq = TERM_SEQ;
            LOG_WRN("[SENSOR] EoP msg sent!");
		} else {
			LOG_INF("[SENSOR] publish seq=%u temp=%s tick=%u",
                    data.seq, temp_str, data.timestamp_ms);
		}

        int ret = 1;
        int tries = 0;


        if (k_sem_take(&slow_down_prod_sem, K_NO_WAIT) == 0) {
            LOG_WRN("[SENSOR] received signal to slow down production!!!");
			period = period * 1.25;
            LOG_WRN("[SENSOR] New period=%u", period);
        }

        while (ret != 0 && tries < MAX_TRIES) {
            k_msleep(period);
            ret = zbus_chan_pub(&sensor_chan, &data, K_MSEC(100));
            tries++;
        }

        if (ret != 0) {
            LOG_WRN("[SENSOR] publish failed ret=%d after %d tries", ret, tries);
        }

		if (i == MSG_COUNT) break;

        k_msleep(period);
    }
    LOG_INF("[SENSOR] done its publishing");
    k_sem_give(&prod_done_sem);
}

/* ================================================================== */
/*  Message subscriber thread (slower logging)                        */
/* ================================================================== */

static void logger_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    k_thread_name_set(k_current_get(), "logger");

    const struct zbus_channel *chan;
    struct sensor_data msg;
    int received = 0;
	int ret = -1;
	uint8_t last_seq = 255;

    while (received <= MSG_COUNT) {
        ret = zbus_sub_wait(&logger_sub, &chan, K_MSEC(1000));
        if (ret != 0) {
            LOG_WRN("[LOGGER-MSG] timeout ret=%d (retrying)", ret);
            continue;
        }

        ret = zbus_chan_read(chan, &msg, K_NO_WAIT);
        if (ret != 0) {
            LOG_WRN("[LOGGER-MSG] chan_read ret=%d", ret);
            continue;
        }

		/* Discard EoP msg */
		if (msg.seq == TERM_SEQ) {
            LOG_WRN("[LOGGER-MSG] received end of production message");
			break;
		}

		/* We don't want to have duplicates */
		if (msg.seq != last_seq) {
			received++;
			char temp_str[10];
			format_temp(temp_str, sizeof(temp_str), msg.temperature_mc);

			LOG_INF("[LOGGER-MSG] seq=%u temp=%s tick=%u latency=%u",
					msg.seq, temp_str, msg.timestamp_ms,
					k_uptime_get_32() - msg.timestamp_ms);
			last_seq = msg.seq;
		}

        if (wdt_channel >= 0) {
            int ret = task_wdt_feed(wdt_channel);
            if (ret != 0) {
                LOG_WRN("[WATCHDOG] feed failed ret=%d", ret);
            } else {
                LOG_INF("[WATCHDOG] fed channel %d", wdt_channel);
            }
        }

        k_msleep(CONSUMER_DELAY);
    }

    LOG_WRN("[LOGGER-MSG] has received %d messages", received);

    if (wdt_channel >= 0) {
        int ret = task_wdt_delete(wdt_channel);
        if (ret != 0) {
            LOG_WRN("[WATCHDOG] delete failed ret=%d", ret);
        } else {
            LOG_INF("[WATCHDOG] deleted channel %d", wdt_channel);
        }
    }
}

static void wdt_callback(int channel_id, void *user_data)
{
    ARG_UNUSED(user_data);
    LOG_WRN("[WATCHDOG] task watchdog fired! channel=%d — consumer stuck!", channel_id);
}

/* ================================================================== */
/*  Health-check thread                                               */
/* ================================================================== */

static void health_check_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    k_thread_name_set(k_current_get(), "health");

    /* Monitor zbus sensor_chan subscriber msgq fill (proxy: free space) */
    for (uint8_t i = 0; i < MAX_HEALTHCHECKS; ++i) {

		/* EoP signal */
        if (k_sem_take(&prod_done_sem, K_NO_WAIT) == 0) {
            break;
        }

        k_msleep(HEALTHCHECK_PERIOD);
        uint32_t free_space = k_msgq_num_free_get(logger_sub.queue);
        /* Capacity ≈ pool size (32) — 75% filled => free <= 8 */
        size_t full_percentage = ((SUB_MAX_QUEUE - free_space) * 100) / SUB_MAX_QUEUE;
        if (full_percentage >= 75) {
            LOG_WRN("[HEALTH] sensor_chan subscriber msgq %zu%% filled (free=%u) — ALERT!",
                     full_percentage, free_space);
			/* Cutting down production rate */
            k_sem_give(&slow_down_prod_sem);
        } else {
            LOG_INF("[HEALTH] check %d: sensor_chan msgq free=%u (%zu%% filled)",
                     i + 1, free_space, full_percentage);
        }
    }
    LOG_INF("[HEALTH] health-check complete");
}

/* ================================================================== */
/*  Threads                                                           */
/* ================================================================== */

K_THREAD_DEFINE(sensor_thread, STACK_SIZE, sensor_thread_fn,
                NULL, NULL, NULL, 5, 0, 0);

K_THREAD_DEFINE(logger_thread, STACK_SIZE, logger_thread_fn,
                NULL, NULL, NULL, 6, 0, 0);

K_THREAD_DEFINE(health_check_thread, STACK_SIZE, health_check_thread_fn,
                NULL, NULL, NULL, 7, 0, 0);

int main(void)
{
    LOG_INF("=== L5 TASK1: zbus event pipeline with watchdog + health-check ===");
    LOG_INF("Sensor (200ms) -> Listener (fast) + Subscriber (1000ms delay)");
    LOG_INF("Watchdog feeds per msg; health-check monitors msgq at 75%% fill");
    LOG_INF("Production slows on health alert; EoP (TERM_SEQ) stops pipeline");
    LOG_INF("==================================================================");

    LOG_INF("[LOGGER-MSG] Message queue capacity: %u",
			k_msgq_num_free_get(logger_sub.queue));

    k_sem_init(&prod_done_sem, 0, 1);
    k_sem_init(&slow_down_prod_sem, 0, 1);

    int ret_init = task_wdt_init(NULL);
    if (ret_init == 0) {
        wdt_channel = task_wdt_add(2000, wdt_callback, NULL);
        LOG_INF("[WATCHDOG] task_wdt initialized, channel=%d", wdt_channel);
    } else {
        LOG_WRN("[WATCHDOG] task_wdt_init failed: %d", ret_init);
        wdt_channel = -1;
    }

    return 0;
}
