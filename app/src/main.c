#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/random/random.h>

LOG_MODULE_REGISTER(l4_task1, LOG_LEVEL_DBG);

#define STACK_SIZE 2048
#define MSG_COUNT 15
#define MAX_TRIES 3
#define SENSOR_PERIOD 180

struct sensor_data {
    int32_t temperature_mc;
    uint32_t timestamp_ms;
    uint8_t seq;
};

static void display_listener_cb(const struct zbus_channel *chan);

/* Listener — fast display update */
ZBUS_LISTENER_DEFINE(display_lis, display_listener_cb);

/* Message subscriber — receives message copies. */
ZBUS_MSG_SUBSCRIBER_DEFINE(logger_sub);

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

    LOG_INF("[DISPLAY-LIS] seq=%u temp=%d mC tick=%u",
            msg->seq, msg->temperature_mc, k_uptime_get_32());
}

/* ================================================================== */
/*  Publisher thread                                                  */
/* ================================================================== */

static void sensor_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    k_thread_name_set(k_current_get(), "sensor");

    for (uint8_t i = 0; i < MSG_COUNT; ++i) {
        struct sensor_data data = {
            .temperature_mc = 24000 + (int32_t)(sys_rand32_get() % 5000),
            .timestamp_ms  = k_uptime_get_32(),
            .seq           = i,
        };

        LOG_INF("[SENSOR] publish seq=%u temp=%d mC",
                data.seq, data.temperature_mc);

        int ret = 1;
        int tries = 0;

        while (ret != 0 && tries < MAX_TRIES) {
            k_msleep(50);
            ret = zbus_chan_pub(&sensor_chan, &data, K_MSEC(100));
            tries++;
        }

        if (ret != 0) {
            LOG_WRN("[SENSOR] publish failed ret=%d after %d tries", ret, tries);
        }

        k_msleep(SENSOR_PERIOD);
    }
    LOG_INF("[SENSOR] done its publishing");
}

/* ================================================================== */
/*  Message subscriber thread (slower logging)                        */
/* ================================================================== */

static void logger_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    k_thread_name_set(k_current_get(), "logger");

    const struct zbus_channel *chan;
    int received = 0;

    while (received < MSG_COUNT) {
        struct sensor_data msg;

        int ret = zbus_sub_wait_msg(&logger_sub, &chan, &msg, K_MSEC(3000));
        if (ret != 0) {
            LOG_WRN("[LOGGER-MSG] timeout ret=%d (retrying)", ret);
            continue;
        }

        received++;
        LOG_INF("[LOGGER-MSG] seq=%u temp=%d mC latency=%ums",
                msg.seq, msg.temperature_mc,
                k_uptime_get_32() - msg.timestamp_ms);

        /* Slower processing — safe with message copy. */
        k_msleep(350);
    }

    LOG_INF("[LOGGER-MSG] done received=%d", received);
}

/* ================================================================== */
/*  Threads                                                           */
/* ================================================================== */

K_THREAD_DEFINE(sensor_thread, STACK_SIZE, sensor_thread_fn,
                NULL, NULL, NULL, 5, 0, 0);

K_THREAD_DEFINE(logger_thread, STACK_SIZE, logger_thread_fn,
                NULL, NULL, NULL, 6, 0, 0);

int main(void)
{
    LOG_INF("======= Lecture 4: Zbus Event-Driven System =======");
    LOG_INF("sensor publishes every %d ms (%d events)", SENSOR_PERIOD, MSG_COUNT);
    LOG_INF("display listener = fast updates (publisher context)");
    LOG_INF("logger subscriber = slower message-copy processing");
    LOG_INF("===================================================");

    return 0;
}
