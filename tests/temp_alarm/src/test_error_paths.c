/*
 * temp_alarm - Error-path unit tests (homework skeleton)
 *
 * Task 2: fill in the stubs according to TEST_SPEC.md. Each stub calls
 * ztest_test_skip(), so the binary builds and runs cleanly until the test is
 * implemented.
 *
 * Run:
 *   west twister -T tests/temp_alarm -p native_sim
 */

#include <errno.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "sensor_fake.h"
#include "temp_alarm.h"

#define DEFAULT_THRESHOLD 30

/* Same before() hook as in test_basic.c: a clean fake and an initialised module. */
static void before(void *fixture)
{
	ARG_UNUSED(fixture);

	sensor_fake_reset();
	sensor_fake_init();

	zassume_ok(temp_alarm_init(&fake_sensor_dev, DEFAULT_THRESHOLD),
		   "precondition: temp_alarm_init must succeed");
}

/*
 * ============================================================================
 * Test Suite: temp_alarm_sensor_errors
 *
 * Failures from the sensor driver, injected through the fakes.
 * ============================================================================
 */
ZTEST_SUITE(temp_alarm_sensor_errors, NULL, NULL, before, NULL, NULL);

ZTEST(temp_alarm_sensor_errors, test_fetch_error_propagates)
{
	struct temp_alarm_status status;

	fake_sensor_sample_fetch_fake.return_val = -EIO;

	zassert_equal(temp_alarm_check(), -EIO, "check must report the fetch error");

	zassert_equal(fake_sensor_channel_get_fake.call_count, 0,
		      "a failed fetch must not be followed by a channel read, got %u calls",
		      fake_sensor_channel_get_fake.call_count);

	zassert_ok(temp_alarm_get_status(&status), "get_status must succeed");
	zassert_equal(status.last_error, -EIO, "the error must be kept in last_error, got %d",
		      status.last_error);
}

ZTEST(temp_alarm_sensor_errors, test_channel_get_error_propagates)
{
	struct temp_alarm_status status;

	fake_sensor_channel_get_fake.custom_fake = NULL;
	fake_sensor_channel_get_fake.return_val = -EIO;

	zassert_equal(temp_alarm_check(), -EIO, "check must report the channel error");

	zassert_equal(fake_sensor_sample_fetch_fake.call_count, 1,
		      "the sample must still have been fetched once, got %u",
		      fake_sensor_sample_fetch_fake.call_count);

	zassert_ok(temp_alarm_get_status(&status), "get_status must succeed");
	zassert_equal(status.last_error, -EIO, "the error must be kept in last_error, got %d",
		      status.last_error);
}

ZTEST(temp_alarm_sensor_errors, test_fail_then_recover)
{
	struct temp_alarm_status status;

	fake_sensor_sample_fetch_fake.return_val = -EIO;
	zassert_equal(temp_alarm_check(), -EIO, "the first check must report the fetch error");

	fake_sensor_sample_fetch_fake.return_val = 0;
	sensor_fake_set_temperature(20);

	zassert_ok(temp_alarm_check(), "the second check must succeed");

	zassert_ok(temp_alarm_get_status(&status), "get_status must succeed");
	zassert_equal(status.last_temp, 20, "the reading must be the recovered one, got %d",
		      status.last_temp);
	zassert_false(status.is_alarming, "20 C is below the 30 C threshold");
	zassert_equal(status.last_error, -EIO,
		      "last_error must keep the failure until a reset, got %d", status.last_error);
}

/*
 * ============================================================================
 * Test Suite: temp_alarm_invalid_input
 *
 * Arguments the public API must reject.
 * ============================================================================
 */
ZTEST_SUITE(temp_alarm_invalid_input, NULL, NULL, before, NULL, NULL);

ZTEST(temp_alarm_invalid_input, test_get_status_null)
{
	zassert_equal(temp_alarm_get_status(NULL), -EINVAL,
		      "get_status must reject a NULL status pointer");
}

ZTEST(temp_alarm_invalid_input, test_init_without_device)
{
	zassert_equal(temp_alarm_init(NULL, DEFAULT_THRESHOLD), -ENODEV,
		      "init without a sensor device must be rejected");
}

ZTEST(temp_alarm_invalid_input, test_init_threshold_out_of_range)
{
	zassert_equal(temp_alarm_init(&fake_sensor_dev, TEMP_ALARM_THRESHOLD_MIN - 1), -EINVAL,
		      "a threshold below the minimum must be rejected");
	zassert_equal(temp_alarm_init(&fake_sensor_dev, TEMP_ALARM_THRESHOLD_MAX + 1), -EINVAL,
		      "a threshold above the maximum must be rejected");
}

ZTEST(temp_alarm_invalid_input, test_set_threshold_out_of_range)
{
	struct temp_alarm_status status;

	zassert_equal(temp_alarm_set_threshold(TEMP_ALARM_THRESHOLD_MIN - 1), -EINVAL,
		      "a threshold below the minimum must be rejected");
	zassert_equal(temp_alarm_set_threshold(TEMP_ALARM_THRESHOLD_MAX + 1), -EINVAL,
		      "a threshold above the maximum must be rejected");

	sensor_fake_set_temperature(DEFAULT_THRESHOLD);
	zassert_ok(temp_alarm_check(), "the check at the threshold must succeed");

	zassert_ok(temp_alarm_get_status(&status), "get_status must succeed");
	zassert_false(status.is_alarming, "30 C must still be at the threshold, not above it");

	sensor_fake_set_temperature(DEFAULT_THRESHOLD + 1);
	zassert_ok(temp_alarm_check(), "the check above the threshold must succeed");

	zassert_ok(temp_alarm_get_status(&status), "get_status must succeed");
	zassert_true(status.is_alarming, "31 C must still be above the threshold");
}
