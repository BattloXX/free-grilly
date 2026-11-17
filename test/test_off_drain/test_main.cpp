#include <unity.h>
#include "OffDrain.h"

void setUp(){}
void tearDown(){}

void test_three_ma_over_a_day(){
    const auto result = OffDrain::compute(1000, 928, 24 * 3600, false);
    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_EQUAL_UINT32(86400, result.off_seconds);
    TEST_ASSERT_EQUAL_INT32(72, result.capacity_drop_mah);
    TEST_ASSERT_EQUAL_INT32(3000, result.drain_ua);
}

void test_zero_drop_is_valid(){
    const auto result = OffDrain::compute(1000, 1000, 3600, false);
    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_EQUAL_INT32(0, result.drain_ua);
}

void test_short_or_invalid_duration(){
    TEST_ASSERT_FALSE(OffDrain::compute(1000, 999, 3599, false).valid);
    TEST_ASSERT_FALSE(OffDrain::compute(1000, 999, -1, false).valid);
    TEST_ASSERT_FALSE(OffDrain::compute(1000, 999, 401LL * 24 * 3600, false).valid);
    const auto short_off = OffDrain::compute(1000, 999, 1800, false);
    TEST_ASSERT_EQUAL_UINT32(1800, short_off.off_seconds);
    TEST_ASSERT_EQUAL_INT32(-1, short_off.drain_ua);
    TEST_ASSERT_TRUE(OffDrain::compute(1000, 999, 400LL * 24 * 3600, false).valid);
}

void test_charged_while_off(){
    const auto result = OffDrain::compute(1000, 1001, 86400, false);
    TEST_ASSERT_FALSE(result.valid);
    TEST_ASSERT_EQUAL_INT32(-1, result.capacity_drop_mah);
    TEST_ASSERT_EQUAL_UINT32(86400, result.off_seconds);
}

void test_charging_now(){
    TEST_ASSERT_FALSE(OffDrain::compute(1000, 928, 86400, true).valid);
}

void test_large_values_and_rounding(){
    const auto result = OffDrain::compute(2000, 0, 300LL * 24 * 3600, false);
    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_EQUAL_INT32(278, result.drain_ua);
    TEST_ASSERT_EQUAL_INT32(65535000, OffDrain::compute(65535, 0, 3600, false).drain_ua);
    TEST_ASSERT_EQUAL_INT32(667, OffDrain::compute(1, 0, 5400, false).drain_ua);
}

int main(int argc, char** argv){
    UNITY_BEGIN();
    RUN_TEST(test_three_ma_over_a_day);
    RUN_TEST(test_zero_drop_is_valid);
    RUN_TEST(test_short_or_invalid_duration);
    RUN_TEST(test_charged_while_off);
    RUN_TEST(test_charging_now);
    RUN_TEST(test_large_values_and_rounding);
    return UNITY_END();
}
