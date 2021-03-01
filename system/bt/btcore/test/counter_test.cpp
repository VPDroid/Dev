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
#include "osi/test/AllocationTestHarness.h"

extern "C" {
#include "btcore/include/counter.h"
#include "btcore/include/module.h"

extern module_t counter_module;
}  // "C"

static const uint64_t COUNTER_TEST_TEN = 10;

typedef struct mycounter_t {
  const char *name;
  uint64_t val;
  bool found;
} mycounter_t;

static bool counter_iter(const char *name, counter_data_t val, void *context) {
  mycounter_t *mycounter = (mycounter_t *)context;
  if (!strcmp(name, mycounter->name)) {
    mycounter->val = val;
    mycounter->found = true;
    return false;
  }
  return true;
}

static bool find_val(const char *name, uint64_t *val) {
  mycounter_t mycounter;

  mycounter.val = 0;
  mycounter.name = name;
  mycounter.found = false;
  counter_foreach(counter_iter, &mycounter);
  *val = mycounter.val;
  if (mycounter.found)
    return true;
  return false;
}

class CounterTest : public AllocationTestHarness {
  protected:
    virtual void SetUp() {
      counter_module.init();
    }

    virtual void TearDown() {
      counter_module.clean_up();
    }
};

TEST_F(CounterTest, counter_no_exist) {
  uint64_t val;

  EXPECT_FALSE(find_val("one.two.three", &val));
}

TEST_F(CounterTest, counter_inc_dec) {
  uint64_t val;

  counter_add("one.two.three", 1);

  EXPECT_TRUE(find_val("one.two.three", &val));
  EXPECT_EQ((uint64_t)1, val);

  counter_add("one.two.three", 1);
  EXPECT_TRUE(find_val("one.two.three", &val));
  EXPECT_EQ((uint64_t)2, val);

  counter_add("one.two.three", -1);
  EXPECT_TRUE(find_val("one.two.three", &val));
  EXPECT_EQ((uint64_t)1, val);
}

TEST_F(CounterTest, counter_get_set) {
  uint64_t val;

  counter_set("one.two.three", COUNTER_TEST_TEN);
  EXPECT_TRUE(find_val("one.two.three", &val));
  EXPECT_EQ(COUNTER_TEST_TEN, val);

  EXPECT_FALSE(find_val("foo.bar", &val));
}
