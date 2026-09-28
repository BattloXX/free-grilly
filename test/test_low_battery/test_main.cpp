#include <unity.h>
#include "LowBattery.h"

static LowBattery lb;

void setUp(){ lb = LowBattery(); }
void tearDown(){}

// Feeds the same reading `count` times, returns true when any of them asked for a switch off
static bool feed(int count, bool read_ok, int percentage, int millivolts, bool charging){
    bool off = false;
    for(int i = 0; i < count; i++){ off = lb.update(read_ok, percentage, millivolts, charging) || off; }
    return off;
}

void test_healthy_battery_stays_on(){
    TEST_ASSERT_FALSE(feed(100, true, 60, 3900, false));
}

void test_low_percentage_switches_off_after_15_readings(){
    TEST_ASSERT_FALSE(feed(14, true, 5, 3450, false));
    TEST_ASSERT_TRUE(lb.update(true, 5, 3450, false));
}

void test_low_voltage_switches_off_even_with_a_higher_percentage(){
    TEST_ASSERT_FALSE(feed(14, true, 12, 3200, false));
    TEST_ASSERT_TRUE(lb.update(true, 12, 3200, false));
}

void test_charging_never_switches_off(){
    TEST_ASSERT_FALSE(feed(100, true, 1, 3100, true));
}

void test_one_good_reading_restarts_the_count(){
    feed(14, true, 4, 3400, false);
    lb.update(true, 6, 3400, false);
    TEST_ASSERT_FALSE(feed(14, true, 4, 3400, false));
    TEST_ASSERT_TRUE(lb.update(true, 4, 3400, false));
}

void test_plugging_in_the_charger_restarts_the_count(){
    feed(14, true, 4, 3400, false);
    lb.update(true, 4, 3400, true);
    TEST_ASSERT_EQUAL_INT(0, lb.low_readings());
}

void test_failed_readings_do_not_count(){
    feed(10, true, 4, 3400, false);
    TEST_ASSERT_FALSE(feed(50, false, 0, 0, false));
    TEST_ASSERT_EQUAL_INT(10, lb.low_readings());
}

void test_low_percentage_with_a_healthy_voltage_is_not_trusted(){
    TEST_ASSERT_FALSE(feed(100, true, 0, 3900, false));
}

void test_low_percentage_without_a_voltage_counts(){
    TEST_ASSERT_TRUE(feed(15, true, 3, 0, false));
}

int main(int argc, char** argv){
    UNITY_BEGIN();
    RUN_TEST(test_healthy_battery_stays_on);
    RUN_TEST(test_low_percentage_switches_off_after_15_readings);
    RUN_TEST(test_low_voltage_switches_off_even_with_a_higher_percentage);
    RUN_TEST(test_charging_never_switches_off);
    RUN_TEST(test_one_good_reading_restarts_the_count);
    RUN_TEST(test_plugging_in_the_charger_restarts_the_count);
    RUN_TEST(test_failed_readings_do_not_count);
    RUN_TEST(test_low_percentage_with_a_healthy_voltage_is_not_trusted);
    RUN_TEST(test_low_percentage_without_a_voltage_counts);
    return UNITY_END();
}
