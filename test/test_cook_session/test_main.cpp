#include <string.h>
#include <unity.h>
#include "CookSession.h"

using namespace history;

static CookSession session;

void setUp(){ session = CookSession(); session.set_salt(0x1a2b3c4d); }
void tearDown(){}

void test_first_sample_starts_session_one(){
    TEST_ASSERT_TRUE(session.update(false, true));
    TEST_ASSERT_TRUE(session.active());
    char id[16];
    session.format_id(id, sizeof(id));
    TEST_ASSERT_EQUAL_STRING("c-1a2b3c4d-0001", id);
}

void test_more_samples_keep_the_session(){
    session.update(false, true);
    TEST_ASSERT_FALSE(session.update(true, true));
    char id[16];
    session.format_id(id, sizeof(id));
    TEST_ASSERT_EQUAL_STRING("c-1a2b3c4d-0001", id);
}

void test_history_cleared_then_data_starts_session_two(){
    session.update(false, true);
    session.update(true, false);
    TEST_ASSERT_TRUE(session.update(false, true));
    char id[16];
    session.format_id(id, sizeof(id));
    TEST_ASSERT_EQUAL_STRING("c-1a2b3c4d-0002", id);
}

void test_empty_samples_do_not_start_a_session(){
    TEST_ASSERT_FALSE(session.update(false, false));
    TEST_ASSERT_FALSE(session.active());
}

int main(int argc, char** argv){
    UNITY_BEGIN();
    RUN_TEST(test_first_sample_starts_session_one);
    RUN_TEST(test_more_samples_keep_the_session);
    RUN_TEST(test_history_cleared_then_data_starts_session_two);
    RUN_TEST(test_empty_samples_do_not_start_a_session);
    return UNITY_END();
}
