#include <gtest/gtest.h>

extern "C" {
#include "atomic.h"
}

const static size_t ATOMIC_DATA_MAX = 2;
const static int ATOMIC_MAX_THREADS = 20;

struct atomic_test_s32_s {
  pthread_t pthread_id;
  int thread_num;
  int max_val;
  atomic_s32_t *data;
};

void *atomic_thread(void *context) {
  struct atomic_test_s32_s *at = (struct atomic_test_s32_s *)context;
  for (int i = 0; i < at->max_val; i++) {
    TEMP_FAILURE_RETRY(usleep(1));
    atomic_inc_prefix_s32(&at->data[i]);
  }
  return NULL;
}

void *atomic_thread_inc_dec(void *context) {
  struct atomic_test_s32_s *at = (struct atomic_test_s32_s *)context;
  for (int i = 0; i < at->max_val; i++) {
    TEMP_FAILURE_RETRY(usleep(1));
    atomic_inc_prefix_s32(&at->data[i]);
    TEMP_FAILURE_RETRY(usleep(1));
    atomic_dec_prefix_s32(&at->data[i]);
  }
  return NULL;
}

TEST(AtomicTest, test_store_load_s32) {
  atomic_s32_t data;

  atomic_store_s32(&data, -1);
  EXPECT_EQ(-1, atomic_load_s32(&data));

  atomic_store_s32(&data, 0);
  EXPECT_EQ(0, atomic_load_s32(&data));

  atomic_store_s32(&data, 1);
  EXPECT_EQ(1, atomic_load_s32(&data));

  atomic_store_s32(&data, 2);
  EXPECT_EQ(2, atomic_load_s32(&data));
}

TEST(AtomicTest, test_store_load_u32) {
  atomic_u32_t data;

  atomic_store_u32(&data, -1);
  EXPECT_EQ((uint32_t)-1, atomic_load_u32(&data));

  atomic_store_u32(&data, 0);
  EXPECT_EQ((uint32_t)0, atomic_load_u32(&data));

  atomic_store_u32(&data, 1);
  EXPECT_EQ((uint32_t)1, atomic_load_u32(&data));

  atomic_store_u32(&data, 2);
  EXPECT_EQ((uint32_t)2, atomic_load_u32(&data));
}

TEST(AtomicTest, test_inc_dec_s32) {
  atomic_s32_t data;

  atomic_store_s32(&data, 0);
  EXPECT_EQ(0, atomic_load_s32(&data));

  int32_t val = atomic_inc_prefix_s32(&data);
  EXPECT_EQ(1, atomic_load_s32(&data));
  EXPECT_EQ(1, val);

  val = atomic_inc_prefix_s32(&data);
  EXPECT_EQ(2, atomic_load_s32(&data));
  EXPECT_EQ(2, val);

  val = atomic_inc_prefix_s32(&data);
  EXPECT_EQ(3, atomic_load_s32(&data));
  EXPECT_EQ(3, val);

  val = atomic_dec_prefix_s32(&data);
  EXPECT_EQ(2, val);

  val = atomic_dec_prefix_s32(&data);
  EXPECT_EQ(1, val);
  val = atomic_dec_prefix_s32(&data);
  EXPECT_EQ(0, val);
  val = atomic_dec_prefix_s32(&data);
  EXPECT_EQ(-1, val);
}

TEST(AtomicTest, test_inc_dec_u32) {
  atomic_u32_t data;

  atomic_store_u32(&data, 0);
  EXPECT_EQ((unsigned)0, atomic_load_u32(&data));

  uint32_t val = atomic_inc_prefix_u32(&data);
  EXPECT_EQ((unsigned)1, atomic_load_u32(&data));
  EXPECT_EQ((unsigned)1, val);

  val = atomic_inc_prefix_u32(&data);
  EXPECT_EQ((unsigned)2, atomic_load_u32(&data));
  EXPECT_EQ((unsigned)2, val);

  val = atomic_inc_prefix_u32(&data);
  EXPECT_EQ((unsigned)3, atomic_load_u32(&data));
  EXPECT_EQ((unsigned)3, val);

  val = atomic_dec_prefix_u32(&data);
  EXPECT_EQ((unsigned)2, val);

  val = atomic_dec_prefix_u32(&data);
  EXPECT_EQ((unsigned)1, val);
  val = atomic_dec_prefix_u32(&data);
  EXPECT_EQ((unsigned)0, val);
  val = atomic_dec_prefix_u32(&data);
  EXPECT_EQ((unsigned)-1, val);
}

TEST(AtomicTest, test_atomic_inc_thread) {
  struct atomic_test_s32_s atomic_test[ATOMIC_MAX_THREADS];
  atomic_s32_t data[ATOMIC_DATA_MAX];

  memset(&atomic_test, 0, sizeof(atomic_test));
  memset(data, 0, sizeof(data));

  for (unsigned int i = 0; i < ATOMIC_DATA_MAX; i++) {
    EXPECT_EQ(0, data[i]._val);
  }

  for (int i = 0; i < ATOMIC_MAX_THREADS; i++) {
    atomic_test[i].thread_num = i;
    atomic_test[i].max_val = ATOMIC_DATA_MAX;
    atomic_test[i].data = data;
    pthread_create(&atomic_test[i].pthread_id, NULL, atomic_thread, &atomic_test[i]);
  }

  for (int i = 0; i < ATOMIC_MAX_THREADS; i++) {
    int rc = pthread_join(atomic_test[i].pthread_id, NULL);
    EXPECT_EQ(0, rc);
  }

  for (unsigned int i = 0; i < ATOMIC_DATA_MAX; i++) {
    EXPECT_EQ(1 * ATOMIC_MAX_THREADS, data[i]._val);
  }
}

TEST(AtomicTest, test_atomic_inc_thread_single) {
  struct atomic_test_s32_s atomic_test[ATOMIC_MAX_THREADS];
  atomic_s32_t data;

  memset(&atomic_test, 0, sizeof(atomic_test));
  memset(&data, 0, sizeof(data));

  EXPECT_EQ(0, data._val);

  for (int i = 0; i < ATOMIC_MAX_THREADS; i++) {
    atomic_test[i].thread_num = i;
    atomic_test[i].max_val = 1;
    atomic_test[i].data = &data;
    pthread_create(&atomic_test[i].pthread_id, NULL, atomic_thread_inc_dec, &atomic_test[i]);
  }

  for (int i = 0; i < ATOMIC_MAX_THREADS; i++) {
    int rc = pthread_join(atomic_test[i].pthread_id, NULL);
    EXPECT_EQ(0, rc);
  }
  EXPECT_EQ(0, data._val);
}

TEST(AtomicTest, test_store_load_s64) {
  atomic_s64_t data;

  atomic_store_s64(&data, -1);
  EXPECT_EQ(-1, atomic_load_s64(&data));

  atomic_store_s64(&data, 0);
  EXPECT_EQ(0, atomic_load_s64(&data));

  atomic_store_s64(&data, 1);
  EXPECT_EQ(1, atomic_load_s64(&data));

  atomic_store_s64(&data, 2);
  EXPECT_EQ(2, atomic_load_s64(&data));
}

TEST(AtomicTest, test_inc_dec_s64) {
  atomic_s64_t data;

  atomic_store_s64(&data, 0);
  EXPECT_EQ(0, atomic_load_s64(&data));

  int64_t val = atomic_inc_prefix_s64(&data);
  EXPECT_EQ(1, atomic_load_s64(&data));
  EXPECT_EQ(1, val);

  val = atomic_inc_prefix_s64(&data);
  EXPECT_EQ(2, atomic_load_s64(&data));
  EXPECT_EQ(2, val);

  val = atomic_inc_prefix_s64(&data);
  EXPECT_EQ(3, atomic_load_s64(&data));
  EXPECT_EQ(3, val);

  val = atomic_dec_prefix_s64(&data);
  EXPECT_EQ(2, val);

  val = atomic_dec_prefix_s64(&data);
  EXPECT_EQ(1, val);
  val = atomic_dec_prefix_s64(&data);
  EXPECT_EQ(0, val);
  val = atomic_dec_prefix_s64(&data);
  EXPECT_EQ(-1, val);

  // Mutating using postfix.
  val = atomic_inc_postfix_s64(&data);
  EXPECT_EQ(0, atomic_load_s64(&data));
  EXPECT_EQ(-1, val);

  val = atomic_inc_postfix_s64(&data);
  EXPECT_EQ(1, atomic_load_s64(&data));
  EXPECT_EQ(0, val);

  val = atomic_inc_postfix_s64(&data);
  EXPECT_EQ(2, atomic_load_s64(&data));
  EXPECT_EQ(1, val);

  val = atomic_dec_postfix_s64(&data);
  EXPECT_EQ(2, val);

  val = atomic_dec_postfix_s64(&data);
  EXPECT_EQ(1, val);
  val = atomic_dec_postfix_s64(&data);
  EXPECT_EQ(0, val);
  val = atomic_dec_postfix_s64(&data);
  EXPECT_EQ(-1, val);
  EXPECT_EQ(-2, atomic_load_s64(&data));
}

TEST(AtomicTest, test_add_sub_u64) {
  atomic_u64_t data;

  atomic_store_u64(&data, 0);
  EXPECT_EQ((unsigned)0, atomic_load_u64(&data));

  uint64_t val = atomic_add_u64(&data, 0xffff);
  EXPECT_EQ((unsigned)0xffff, atomic_load_u64(&data));
  EXPECT_EQ((unsigned)0xffff, val);

  val = atomic_add_u64(&data, 0xffff);
  EXPECT_EQ((unsigned)(2 * 0xffff), atomic_load_u64(&data));
  EXPECT_EQ((unsigned)(2 * 0xffff), val);

  val = atomic_add_u64(&data, 0xffff);
  EXPECT_EQ((unsigned)(3 * 0xffff), atomic_load_u64(&data));
  EXPECT_EQ((unsigned)(3 * 0xffff), val);
  EXPECT_NE((unsigned)(3 * 0xfff0), val);

  val = atomic_sub_u64(&data, 0xffff);
  EXPECT_EQ((unsigned)(2 * 0xffff), val);

  val = atomic_sub_u64(&data, 0);
  EXPECT_EQ((unsigned)(2 * 0xffff), val);
  val = atomic_sub_u64(&data, 0xffff);
  EXPECT_EQ((unsigned)(1 * 0xffff), val);

  val = atomic_sub_u64(&data, 0xffff);
  EXPECT_EQ((unsigned)0, val);
}


