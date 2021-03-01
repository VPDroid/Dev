/******************************************************************************
 *
 *  Copyright (C) 2014 Google, Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

#include <gtest/gtest.h>

#include "AlarmTestHarness.h"

extern "C" {
#include "alarm.h"
#include "osi.h"
#include "semaphore.h"
}

static semaphore_t *semaphore;
static int cb_counter;

static const uint64_t EPSILON_MS = 5;

static void msleep(uint64_t ms) {
  TEMP_FAILURE_RETRY(usleep(ms * 1000));
}

class AlarmTest : public AlarmTestHarness {
  protected:
    virtual void SetUp() {
      AlarmTestHarness::SetUp();
      cb_counter = 0;

      semaphore = semaphore_new(0);
    }

    virtual void TearDown() {
      semaphore_free(semaphore);
      AlarmTestHarness::TearDown();
    }
};

static void cb(UNUSED_ATTR void *data) {
  ++cb_counter;
  semaphore_post(semaphore);
}

TEST_F(AlarmTest, test_new_free_simple) {
  alarm_t *alarm = alarm_new();
  ASSERT_TRUE(alarm != NULL);
  alarm_free(alarm);
}

TEST_F(AlarmTest, test_free_null) {
  alarm_free(NULL);
}

TEST_F(AlarmTest, test_simple_cancel) {
  alarm_t *alarm = alarm_new();
  alarm_cancel(alarm);
  alarm_free(alarm);
}

TEST_F(AlarmTest, test_cancel) {
  alarm_t *alarm = alarm_new();
  alarm_set(alarm, 10, cb, NULL);
  alarm_cancel(alarm);

  msleep(10 + EPSILON_MS);

  EXPECT_EQ(cb_counter, 0);
  EXPECT_EQ(lock_count, 0);
  alarm_free(alarm);
}

TEST_F(AlarmTest, test_cancel_idempotent) {
  alarm_t *alarm = alarm_new();
  alarm_set(alarm, 10, cb, NULL);
  alarm_cancel(alarm);
  alarm_cancel(alarm);
  alarm_cancel(alarm);
  alarm_free(alarm);
}

TEST_F(AlarmTest, test_set_short) {
  alarm_t *alarm = alarm_new();
  alarm_set(alarm, 10, cb, NULL);

  EXPECT_EQ(cb_counter, 0);
  EXPECT_EQ(lock_count, 1);

  semaphore_wait(semaphore);

  EXPECT_EQ(cb_counter, 1);
  EXPECT_EQ(lock_count, 0);

  alarm_free(alarm);
}

TEST_F(AlarmTest, test_set_long) {
  alarm_t *alarm = alarm_new();
  alarm_set(alarm, TIMER_INTERVAL_FOR_WAKELOCK_IN_MS, cb, NULL);

  EXPECT_EQ(cb_counter, 0);
  EXPECT_EQ(lock_count, 0);

  semaphore_wait(semaphore);

  EXPECT_EQ(cb_counter, 1);
  EXPECT_EQ(lock_count, 0);

  alarm_free(alarm);
}

TEST_F(AlarmTest, test_set_short_short) {
  alarm_t *alarm[2] = {
    alarm_new(),
    alarm_new()
  };

  alarm_set(alarm[0], 10, cb, NULL);
  alarm_set(alarm[1], 20, cb, NULL);

  EXPECT_EQ(cb_counter, 0);
  EXPECT_EQ(lock_count, 1);

  semaphore_wait(semaphore);

  EXPECT_EQ(cb_counter, 1);
  EXPECT_EQ(lock_count, 1);

  semaphore_wait(semaphore);

  EXPECT_EQ(cb_counter, 2);
  EXPECT_EQ(lock_count, 0);

  alarm_free(alarm[0]);
  alarm_free(alarm[1]);
}

TEST_F(AlarmTest, test_set_short_long) {
  alarm_t *alarm[2] = {
    alarm_new(),
    alarm_new()
  };

  alarm_set(alarm[0], 10, cb, NULL);
  alarm_set(alarm[1], 10 + TIMER_INTERVAL_FOR_WAKELOCK_IN_MS + EPSILON_MS, cb, NULL);

  EXPECT_EQ(cb_counter, 0);
  EXPECT_EQ(lock_count, 1);

  semaphore_wait(semaphore);

  EXPECT_EQ(cb_counter, 1);
  EXPECT_EQ(lock_count, 0);

  semaphore_wait(semaphore);

  EXPECT_EQ(cb_counter, 2);
  EXPECT_EQ(lock_count, 0);

  alarm_free(alarm[0]);
  alarm_free(alarm[1]);
}

TEST_F(AlarmTest, test_set_long_long) {
  alarm_t *alarm[2] = {
    alarm_new(),
    alarm_new()
  };

  alarm_set(alarm[0], TIMER_INTERVAL_FOR_WAKELOCK_IN_MS, cb, NULL);
  alarm_set(alarm[1], 2 * TIMER_INTERVAL_FOR_WAKELOCK_IN_MS + EPSILON_MS, cb, NULL);

  EXPECT_EQ(cb_counter, 0);
  EXPECT_EQ(lock_count, 0);

  semaphore_wait(semaphore);

  EXPECT_EQ(cb_counter, 1);
  EXPECT_EQ(lock_count, 0);

  semaphore_wait(semaphore);

  EXPECT_EQ(cb_counter, 2);
  EXPECT_EQ(lock_count, 0);

  alarm_free(alarm[0]);
  alarm_free(alarm[1]);
}

// Try to catch any race conditions between the timer callback and |alarm_free|.
TEST_F(AlarmTest, test_callback_free_race) {
  for (int i = 0; i < 1000; ++i) {
    alarm_t *alarm = alarm_new();
    alarm_set(alarm, 0, cb, NULL);
    alarm_free(alarm);
  }
}
