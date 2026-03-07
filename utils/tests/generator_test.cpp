#include "utils/generator.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <barrier>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace to = task_orchestrator;

template <typename T>
struct GeneratorTraits;

template <>
struct GeneratorTraits<int> {
  static to::Generator<int> make() {
    return []() -> to::Generator<int> {
      co_yield 1;
      co_yield 2;
      co_yield 3;
    }();
  }
  static std::vector<int> expected() { return {1, 2, 3}; }
};

template <>
struct GeneratorTraits<size_t> {
  static to::Generator<size_t> make() {
    return []() -> to::Generator<size_t> {
      co_yield 10U;
      co_yield 20U;
      co_yield 30U;
    }();
  }
  static std::vector<size_t> expected() { return {10U, 20U, 30U}; }
};

template <>
struct GeneratorTraits<std::string> {
  static to::Generator<std::string> make() {
    return []() -> to::Generator<std::string> {
      co_yield "a";
      co_yield "b";
      co_yield "c";
    }();
  }
  static std::vector<std::string> expected() { return {"a", "b", "c"}; }
};

using GeneratorTypes = ::testing::Types<int, size_t, std::string>;

template <typename T>
class GeneratorTypeTest : public ::testing::Test {};
TYPED_TEST_SUITE(GeneratorTypeTest, GeneratorTypes);

TYPED_TEST(GeneratorTypeTest, YieldsValuesInOrder) {
  auto gen = GeneratorTraits<TypeParam>::make();
  std::vector<TypeParam> actual;
  for (const auto& value : gen) {
    actual.push_back(value);
  }
  EXPECT_EQ(actual, GeneratorTraits<TypeParam>::expected());
}

TYPED_TEST(GeneratorTypeTest, MoveConstructor) {
  auto gen = GeneratorTraits<TypeParam>::make();
  auto gen2 = std::move(gen);
  std::vector<TypeParam> actual;
  for (const auto& value : gen2) {
    actual.push_back(value);
  }
  EXPECT_EQ(actual, GeneratorTraits<TypeParam>::expected());
}

TYPED_TEST(GeneratorTypeTest, MoveAssignment) {
  auto gen = GeneratorTraits<TypeParam>::make();
  auto gen2 = []() -> to::Generator<TypeParam> { co_yield GeneratorTraits<TypeParam>::expected()[0]; }();
  gen2 = std::move(gen);
  std::vector<TypeParam> actual;
  for (const auto& value : gen2) {
    actual.push_back(value);
  }
  EXPECT_EQ(actual, GeneratorTraits<TypeParam>::expected());
}

TYPED_TEST(GeneratorTypeTest, IteratorAdvance) {
  auto gen = GeneratorTraits<TypeParam>::make();
  auto it = gen.begin();
  ASSERT_NE(it, gen.end());
  EXPECT_EQ(*it, GeneratorTraits<TypeParam>::expected()[0]);
  ++it;
  ASSERT_NE(it, gen.end());
  EXPECT_EQ(*it, GeneratorTraits<TypeParam>::expected()[1]);
  ++it;
  ASSERT_NE(it, gen.end());
  EXPECT_EQ(*it, GeneratorTraits<TypeParam>::expected()[2]);
  ++it;
  EXPECT_FALSE(it != gen.end());
}

TEST(GeneratorTest, EmptyGenerator) {
  auto gen = []() -> to::Generator<int> {
    // yields nothing
    co_return;
  }();
  size_t count = 0;
  for ([[maybe_unused]] int x : gen) {
    ++count;
  }
  EXPECT_EQ(count, 0U);
}

TEST(GeneratorTest, PropagatesException) {
  auto gen = []() -> to::Generator<int> {
    co_yield 1;
    throw std::runtime_error("test error");
    co_yield 2;
  }();
  std::vector<int> seen;
  // NOLINTNEXTLINE(modernize-type-traits)
  EXPECT_THROW(
      {
        for (int x : gen) {
          seen.push_back(x);
        }
      },
      std::runtime_error);
  // NOLINTEND(modernize-type-traits)
  EXPECT_EQ(seen, std::vector<int>{1});
}

TEST(GeneratorTest, ConcurrentConsumptionWithBarrier) {
  constexpr unsigned kNumThreads = 4;
  constexpr int kValuesPerGenerator = 10;

  const auto make_gen = []() -> to::Generator<int> {
    return []() -> to::Generator<int> {
      for (int i = 0; i < kValuesPerGenerator; ++i) {
        co_yield i;
      }
    }();
  };

  std::barrier sync{kNumThreads};
  std::mutex mu;
  std::vector<std::vector<int>> results(kNumThreads);

  auto worker = [&](unsigned idx) {
    auto gen = make_gen();
    sync.arrive_and_wait();
    std::vector<int> local;
    for (int value : gen) {
      local.push_back(value);
    }
    {
      std::scoped_lock lock(mu);
      results[idx] = std::move(local);
    }
  };

  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (unsigned i = 0; i < kNumThreads; ++i) {
    threads.emplace_back(worker, i);
  }
  for (auto& t : threads) {
    t.join();
  }

  std::vector<int> expected(kValuesPerGenerator);
  for (int i = 0; i < kValuesPerGenerator; ++i) {
    expected[i] = i;
  }
  for (const auto& r : results) {
    EXPECT_EQ(r, expected) << "Thread produced wrong sequence";
  }
}

TEST(GeneratorTest, BarrierSyncThenGeneratePerThread) {
  constexpr unsigned kNumThreads = 3;

  std::barrier sync{kNumThreads};
  std::mutex mu;
  std::vector<std::string> order;

  auto worker = [&](unsigned /* id */, const std::string& label) {
    sync.arrive_and_wait();
    auto gen = [label]() -> to::Generator<std::string> {
      co_yield label + "_1";
      co_yield label + "_2";
    }();
    for (const auto& s : gen) {
      std::scoped_lock lock(mu);
      order.push_back(s);
    }
  };

  std::thread t1(worker, 0, "A");
  std::thread t2(worker, 1, "B");
  std::thread t3(worker, 2, "C");
  t1.join();
  t2.join();
  t3.join();

  // Each thread must have produced its two values.
  EXPECT_EQ(order.size(), 6U);
  std::vector<std::string> sorted_order = order;
  std::ranges::sort(sorted_order);
  EXPECT_EQ(sorted_order, (std::vector<std::string>{"A_1", "A_2", "B_1", "B_2", "C_1", "C_2"}));
}

}  // namespace
