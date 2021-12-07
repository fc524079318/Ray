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
#include <boost/asio/error.hpp>
#include <list>

// clang-format off
#include "ray/raylet/node_manager.h"
#include "ray/object_manager/object_manager.h"
#include "ray/common/task/scheduling_resources.h"
#include "ray/common/asio/instrumented_io_context.h"
// clang-format on

namespace ray {

namespace raylet {

usingrpc::GcsNodeInfo;

class NodeManager;

class Raylet {
 public:
  /// Create a raylet server and listen for local clients.
  ///
  /// \param main_service The event loop to run the server on. 循环运行服务
  /// \param object_manager_service The asio io_service tied to the object manager. 基于asio的对象管理
  /// \param socket_name The Unix domain socket to listen on for local clients.监听本地客户端的Unix domain socket名称
  /// \param node_ip_address The IP address of this node.该节点的ip地址
  /// \param redis_address The IP address of the redis instance we are connecting to.日志redis的IP地址
  /// \param redis_port The port of the redis instance we are connecting to.日志接口的端口号
  /// \param redis_password The password of the redis instance we are connecting to.日志接口的密码
  /// \param node_manager_config Configuration to initialize the node manager.节点管理的配置
  /// scheduler with.调度程序
  /// \param object_manager_config Configuration to initialize the object对象初始化的配置
  /// manager.
  /// \param gcs_client A client connection to the GCS.一个连接到gcs的客户端
  /// \param metrics_export_port A port at which metrics are exposed to.metrice的暴露端口
  Raylet(instrumented_io_context &main_service, const std::string &socket_name,
         const std::string &node_ip_address, const std::string &redis_address,
         int redis_port, const std::string &redis_password,
         const NodeManagerConfig &node_manager_config,
         const ObjectManagerConfig &object_manager_config,
         std::shared_ptr<gcs::GcsClient> gcs_client, int metrics_export_port);

  /// Start this raylet.
  void Start();

  /// Stop this raylet.
  void Stop();

  /// Destroy the NodeServer.
  ~Raylet();

  NodeID GetNodeId() const { return self_node_id_; }

 private:
  /// Register GCS client.
  ray::Status RegisterGcs();

  /// Accept a client connection.
  void DoAccept();
  /// Handle an accepted client connection.
  void HandleAccept(const boost::system::error_code &error);

  friend class TestObjectManagerIntegration;

  // Main event loop.
  instrumented_io_context &main_service_;

  /// ID of this node.
  NodeID self_node_id_;
  /// Information of this node.
  GcsNodeInfo self_node_info_;

  /// A client connection to the GCS.
  std::shared_ptr<gcs::GcsClient> gcs_client_;
  /// Manages client requests for task submission and execution.
  NodeManager node_manager_;
  /// The name of the socket this raylet listens on.
  std::string socket_name_;

  /// An acceptor for new clients.
  boost::asio::basic_socket_acceptor<local_stream_protocol> acceptor_;
  /// The socket to listen on for new clients.
  local_stream_socket socket_;
};

}  // namespace raylet

}  // namespace ray
