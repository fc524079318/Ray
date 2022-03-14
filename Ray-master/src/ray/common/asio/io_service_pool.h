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

#include <atomic>
#include <boost/asio.hpp>
#include <thread>
#include "ray/common/asio/instrumented_io_context.h"

namespace ray {

/// 类 IOServicePool
/// io_service池。每个io_service都拥有一个线程
/// 要从该池获取io_service，需要先调用'Run()'
/// 再退出前必须调用'Stop()'
/// \class IOServicePool
/// The io_service pool. Each io_service owns a thread.
/// To get io_service from this pool should call `Run()` first.
/// Before exit, `Stop()` must be called.
class IOServicePool {
 public:
  IOServicePool(size_t io_service_num);

  ~IOServicePool();

  void Run();

  void Stop();

  /// 通过循环选择io_service
  /// 返回 io_service
  /// Select io_service by round robin.
  ///
  /// \return io_service
  instrumented_io_context *Get();

  /// 通过哈希获取io_service
  /// 参数 hash 使用这个hash来获取io_service
  /// 相同的哈希总会获得相同的io_service
  /// 返回 io_service
  /// Select io_service by hash.
  ///
  /// \param hash Use this hash to pick a io_service.
  /// The same hash will alway get the same io_service.
  /// \return io_service
  instrumented_io_context *Get(size_t hash);

  /// 获取所有io_service
  /// 这只用于RedisClient::Connect()
  /// Get all io_service.
  /// This is only use for RedisClient::Connect().
  std::vector<instrumented_io_context *> GetAll();

 private:
  size_t io_service_num_{0};

  std::vector<std::thread> threads_;
  std::vector<std::unique_ptr<instrumented_io_context>> io_services_;

  std::atomic<size_t> current_index_;
};

inline instrumented_io_context *IOServicePool::Get() {
  size_t index = ++current_index_ % io_service_num_;
  return io_services_[index].get();
}

inline instrumented_io_context *IOServicePool::Get(size_t hash) {
  size_t index = hash % io_service_num_;
  return io_services_[index].get();
}

inline std::vector<instrumented_io_context *> IOServicePool::GetAll() {
  std::vector<instrumented_io_context *> io_services;
  for (auto &io_service : io_services_) {
    io_services.emplace_back(io_service.get());
  }
  return io_services;
}

}  // namespace ray
