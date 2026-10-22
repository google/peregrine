#include "peregrine/src/internal/trace/trace_buffer.h"

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"
#include "peregrine/src/internal/trace/trace_event.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::internal::testing {
namespace {

TEST(TraceBufferTest, RingWraparound) {
  TraceBuffer buf;
  EXPECT_EQ(buf.Capacity(), TraceBuffer::kCapacity);
  EXPECT_EQ(buf.TotalRecorded(), 0);
  EXPECT_TRUE(buf.Snapshot().empty());

  constexpr uint32_t kExtraEvents = 18;
  const uint32_t total_events =
      static_cast<uint32_t>(TraceBuffer::kCapacity) + kExtraEvents;
  for (uint32_t i = 1; i <= total_events; ++i) {
    buf.Record(TraceEvent{
        .timestamp_us = 1000 + i,
        .handle = i,
        .reqid = i * 10,
        .chunk_index = 0,
        .duration_us = i,
        .aux_value = 4096,
        .worker_id = 1,
        .stage = TraceStage::kPostEntry,
        .flags = 0,
    });
  }

  EXPECT_EQ(buf.TotalRecorded(), total_events);
  const std::vector<TraceEvent> snap = buf.Snapshot();
  ASSERT_EQ(snap.size(), TraceBuffer::kCapacity);
  EXPECT_EQ(snap.front().handle, kExtraEvents + 1);
  EXPECT_EQ(snap.back().handle, total_events);

  buf.Clear();
  EXPECT_EQ(buf.TotalRecorded(), 0);
  EXPECT_TRUE(buf.Snapshot().empty());
}

TEST(TraceBufferTest, ConcurrentMultiThreadedRecord) {
  TraceBuffer buf;
  constexpr int kNumThreads = 4;
  constexpr int kEventsPerThread = 200;

  std::vector<util::Thread> threads;
  threads.reserve(kNumThreads);
  for (int t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&buf, t]() {
      for (int i = 1; i <= kEventsPerThread; ++i) {
        buf.Record(TraceEvent{
            .timestamp_us = static_cast<uint64_t>(t * 1000 + i),
            .handle = static_cast<uint32_t>(t + 1),
            .reqid = static_cast<uint32_t>(i),
            .chunk_index = 0,
            .duration_us = 10,
            .aux_value = 1024,
            .worker_id = static_cast<int16_t>(t + 1),
            .stage = TraceStage::kWorkerDequeued,
            .flags = 0,
        });
      }
    });
  }
  for (auto& th : threads) {
    th.join();
  }

  EXPECT_EQ(buf.TotalRecorded(), kNumThreads * kEventsPerThread);
  const auto snap = buf.Snapshot();
  EXPECT_EQ(snap.size(), kNumThreads * kEventsPerThread);
}

}  // namespace
}  // namespace peregrine::internal::testing
