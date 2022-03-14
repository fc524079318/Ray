// Copyright 2017 The Ray Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <boost/asio.hpp>
#include <limits>
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "ray/common/ray_config.h"
#include "ray/util/logging.h"

/// event loop 事件循环，是一种异步机制
/// asio处理程序的计数、排队和执行统计数据 
/// Count, queueing, and execution statistics for an asio handler.
struct HandlerStats {
  // 计数
  // Counts.
  int64_t cum_count = 0;
  int64_t curr_count = 0;

  // 执行统计
  // Execution stats.
  int64_t cum_execution_time = 0;
  int64_t running_count = 0;
};

/// 对所有asio处理程序进行统计和排队统计
/// Count and queueing statistics over all asio handlers.
struct GlobalStats {
  // Queue stats.
  int64_t cum_queue_time = 0;
  int64_t min_queue_time = std::numeric_limits<int64_t>::max();
  int64_t max_queue_time = -1;
};

/// 关于处理程序status结构的一个互斥锁(部分handler)
/// A mutex wrapper around a handler stats struct.
struct GuardedHandlerStats {
  // 一些处理程序的统计数据
  // Stats for some handler.
  HandlerStats stats;

  // 用于保护这些数据的读取和写入的互斥器。
  // 在阅读前,该互斥对象应使用reader锁定,并在写入前应该使用writer锁。
  // The mutex protecting the reading and writing of these stats.
  // This mutex should be acquired with a reader lock before reading, and should be
  // acquired with a writer lock before writing.
  mutable absl::Mutex mutex;
};

/// 一个处理程序stats结构的互斥包。(所有的handler)
/// A mutex wrapper around a handler stats struct.
struct GuardedGlobalStats {
  // 统计所有处理程序
  // Stats over all handlers.
  GlobalStats stats;

  // 用于保护这些数据的读取和写入的互斥器。
  // 在阅读前,该互斥对象应使用reader锁定,并在写入前应该使用writer锁。
  // The mutex protecting the reading and writing of these stats.
  // This mutex should be acquired with a reader lock before reading, and should be
  // acquired with a writer lock before writing.
  mutable absl::Mutex mutex;
};

/// 一个不透明的统计句柄，用于手动处理没有连结到一个.post() 调用的仪表事件循环处理系统
//  未完成：从asio中获得跟踪器
/// An opaque stats handle, used to manually instrument event loop handlers that don't
/// hook into a .post() call.
// TODO Pull out the tracker from ASIO
struct StatsHandle {
  std::string handler_name;
  int64_t start_time;
  std::shared_ptr<GuardedHandlerStats> handler_stats;
  std::shared_ptr<GuardedGlobalStats> global_stats;
  bool execution_recorded = false;

  StatsHandle(std::string handler_name_, int64_t start_time_,
              std::shared_ptr<GuardedHandlerStats> handler_stats_,
              std::shared_ptr<GuardedGlobalStats> global_stats_)
      : handler_name(std::move(handler_name_)),
        start_time(start_time_),
        handler_stats(std::move(handler_stats_)),
        global_stats(std::move(global_stats_)) {}

  void ZeroAccumulatedQueuingDelay() { start_time = absl::GetCurrentTimeNanos(); }

  ~StatsHandle() {
    if (!execution_recorded) {
	  // ?如果不记录处理程序的执行,我们需要清理一些排队的统计数据,以防止这些数据泄漏。 
      // If handler execution was never recorded, we need to clean up some queueing
      // stats in order to prevent those stats from leaking.
      absl::MutexLock lock(&(handler_stats->mutex));
      handler_stats->stats.curr_count--;
    }
  }
};

/// 一个boost::asio::io_context的代理，用于收集发布处理程序的统计数据
/// A proxy for boost::asio::io_context that collects statistics about posted handlers.
class instrumented_io_context : public boost::asio::io_context {
 public:
  /// 在调用base逆变器后初始化全局stats结构。
  /// Initializes the global stats struct after calling the base contructor.
  instrumented_io_context() : global_stats_(std::make_shared<GuardedGlobalStats>()) {}

  /// 为给定处理程序收集计数、排队和执行统计的代理post函数
  /// 参数 handler 要被发布到事件循环的处理程序。
  /// 参数 name 人类读得懂的处理程序名称,用于浏览被提供的处理程序的统计数据。默认值为未知。 
  /// A proxy post function that collects count, queueing, and execution statistics for
  /// the given handler.
  ///
  /// \param handler The handler to be posted to the event loop.
  /// \param name A human-readable name for the handler, to be used for viewing stats
  /// for the provided handler. Defaults to UNKNOWN.
  void post(std::function<void()> handler, const std::string name = "UNKNOWN")
      LOCKS_EXCLUDED(mutex_);

  /// 操作开始手动记录的一个代理post函数。例如,这对于跟踪主动出站RPC调用的数量很有用。
  ///
  /// 参数 handler 被发布到事件循环的处理程序
  /// 参数 handle 之前通过RecordStart()返回的stats句柄。 
  /// A proxy post function where the operation start is manually recorded. For example,
  /// this is useful for tracking the number of active outbound RPC calls.
  ///
  /// \param handler The handler to be posted to the event loop.
  /// \param handle The stats handle returned by RecordStart() previously.
  void post(std::function<void()> handler, std::shared_ptr<StatsHandle> handle)
      LOCKS_EXCLUDED(mutex_);

  /// 为给定处理程序收集计数、排队和执行的统计数据的代理post函数。
  /// 参数 handler 被送到事件循环的处理程序
  /// 参数 name 人类读得懂的处理程序名称,用于浏览被提供的处理程序的统计数据。默认值为未知。
  /// A proxy post function that collects count, queueing, and execution statistics for
  /// the given handler.
  ///
  /// \param handler The handler to be posted to the event loop.
  /// \param name A human-readable name for the handler, to be used for viewing stats
  /// for the provided handler. Defaults to UNKNOWN.
  void dispatch(std::function<void()> handler, const std::string name = "UNKNOWN")
      LOCKS_EXCLUDED(mutex_);

  /// 设置排队启动时间,增加当前和累计计数,并返回不透明的处理数据。
  /// 这用于与recordexexreduce()一起使用,以手动配备一个不调用. post()的事件循环处理程序。 
  ///
  /// 返回的不透明数据句柄应该被给随后的RecordExecution()调用。
  ///
  /// 参数 name 与收集的数据相关的人能看懂的名称
  /// 参数 expected_queueing_delay_ns 以ns为单位的在队列中的排队时长
  /// 返回 用于给RecordExecution()的不透明stats句柄
  /// Sets the queueing start time, increments the current and cumulative counts and
  /// returns an opaque handle for these stats. This is used in conjunction with
  /// RecordExecution() to manually instrument an event loop handler that doesn't call
  /// .post().
  ///
  /// The returned opaque stats handle should be given to a subsequent RecordExecution()
  /// call.
  ///
  /// \param name A human-readable name to which collected stats will be associated.
  /// \param expected_queueing_delay_ns How much to pad the observed queueing start time,
  ///  in nanoseconds.
  /// \return An opaque stats handle, to be given to RecordExecution().
  std::shared_ptr<StatsHandle> RecordStart(const std::string &name,
                                           int64_t expected_queueing_delay_ns = 0);
  /// 记录提供的函数执行的统计数据。这是与RecordStart()一起使用的,用于手动处理不调用. post()的事件循环处理程序。 
  ///
  /// 参数 fn 要执行和装备的函数
  /// 参数 handle 由RecordStart()返回的一个不透明的stats句柄。 
  /// Records stats about the provided function's execution. This is used in conjunction
  /// with RecordStart() to manually instrument an event loop handler that doesn't call
  /// .post().
  ///
  /// \param fn The function to execute and instrument.
  /// \param handle An opaque stats handle returned by RecordStart().
  static void RecordExecution(const std::function<void()> &fn,
                              std::shared_ptr<StatsHandle> handle);

  /// 返回所有处理程序的全局计数、排队和执行统计的快照视图。 
  ///
  /// 返回 全球处理程序统计数据的快照视图。 
  /// Returns a snapshot view of the global count, queueing, and execution statistics
  /// across all handlers.
  ///
  /// \return A snapshot view of the global handler stats.
  GlobalStats get_global_stats() const;

  /// 返回所提供处理程序的计数、排队和执行统计信息的快照视图。
  /// 参数 handler_name 应该被返回的处理程序
  /// 返回 处理程序状态的快照视图。
  /// Returns a snapshot view of the count, queueing, and execution statistics for the
  /// provided handler.
  ///
  /// \param handler_name The name of the handler whose stats should be returned.
  /// \return A snapshot view of the handler's stats.
  absl::optional<HandlerStats> get_handler_stats(const std::string &handler_name) const
      LOCKS_EXCLUDED(mutex_);

  /// 返回所有处理程序的计数、排队和执行统计的快照视图。 
  /// 返回 所有处理程序状态的快照视图
  /// Returns snapshot views of the count, queueing, and execution statistics for all
  /// handlers.
  ///
  /// \return A vector containing snapshot views of stats for all handlers.
  std::vector<std::pair<std::string, HandlerStats>> get_handler_stats() const
      LOCKS_EXCLUDED(mutex_);

  /// 生成并返回一个统计摘要字符串。
  /// 由使用这个io_context包装器的对象的DebugString()使用，比如raylet和core worker。
  ///
  /// 返回 一个统计汇总字符串，适合包含在对象的DebugString()中  
  /// Builds and returns a statistics summary string. Used by the DebugString() of
  /// objects that used this io_context wrapper, such as the raylet and the core worker.
  ///
  /// \return A stats summary string, suitable for inclusion in an object's
  /// DebugString().
  std::string StatsString() const LOCKS_EXCLUDED(mutex_);

 private:
  using HandlerStatsTable =
      absl::flat_hash_map<std::string, std::shared_ptr<GuardedHandlerStats>>;
  /// 如果该处理程序存在，则获取该处理程序的互斥保护状态，
  /// 否则为该处理程序创建状态并返回指向该处理程序的迭代器。
  ///
  /// 参数 name 人类读的懂得处理程序名称，用于查看所提供的处理程序的统计信息。  
  /// Get the mutex-guarded stats for this handler if it exists, otherwise create the
  /// stats for this handler and return an iterator pointing to it.
  ///
  /// \param name A human-readable name for the handler, to be used for viewing stats
  /// for the provided handler.
  std::shared_ptr<GuardedHandlerStats> GetOrCreate(const std::string &name);

  /// 跨所有处理程序的全局统计。
  /// Global stats, across all handlers.
  std::shared_ptr<GuardedGlobalStats> global_stats_;

  /// 每个处理程序发布的统计信息表。
  /// 我们使用std::shared_ptr值来确保指针的稳定性。
  /// Table of per-handler post stats.
  /// We use a std::shared_ptr value in order to ensure pointer stability.
  HandlerStatsTable post_handler_stats_ GUARDED_BY(mutex_);

  /// 保护对每个处理器发布状态表的访问。
  /// Protects access to the per-handler post stats table.
  mutable absl::Mutex mutex_;
};
