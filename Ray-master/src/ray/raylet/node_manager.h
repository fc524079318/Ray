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

// clang-format off
#include "ray/rpc/grpc_client.h"
#include "ray/rpc/node_manager/node_manager_server.h"
#include "ray/rpc/node_manager/node_manager_client.h"
#include "ray/common/id.h"
#include "ray/common/task/task.h"
#include "ray/common/ray_object.h"
#include "ray/common/client_connection.h"
#include "ray/common/task/task_common.h"
#include "ray/common/task/scheduling_resources.h"
#include "ray/pubsub/subscriber.h"
#include "ray/object_manager/object_manager.h"
#include "ray/raylet/agent_manager.h"
#include "ray/raylet_client/raylet_client.h"
#include "ray/common/runtime_env_manager.h"
#include "ray/raylet/local_object_manager.h"
#include "ray/raylet/scheduling/scheduling_ids.h"
#include "ray/raylet/scheduling/cluster_resource_scheduler.h"
#include "ray/raylet/scheduling/cluster_task_manager.h"
#include "ray/raylet/scheduling/cluster_task_manager_interface.h"
#include "ray/raylet/dependency_manager.h"
#include "ray/raylet/worker_pool.h"
#include "ray/rpc/worker/core_worker_client_pool.h"
#include "ray/util/ordered_set.h"
#include "ray/util/throttler.h"
#include "ray/common/asio/instrumented_io_context.h"
#include "ray/common/bundle_spec.h"
#include "ray/raylet/placement_group_resource_manager.h"
// clang-format on

namespace ray {

namespace raylet {

using rpc::ActorTableData;
using rpc::ErrorType;
using rpc::GcsNodeInfo;
using rpc::HeartbeatTableData;
using rpc::JobTableData;
using rpc::ResourceUsageBatchData;

struct NodeManagerConfig {
  /// 进行了节点资源配置
  /// The node's resource configuration.
  ResourceSet resource_config;
  /// 节点管理运行所在的IP地址
  /// The IP address this node manager is running on.
  std::string node_manager_address;
  /// 用来监听接入连接的端口
  /// 若为0则选择自身的端口
  /// The port to use for listening to incoming connections. If this is 0 then
  /// the node manager will choose its own port.
  int node_manager_port;
  /// workers启动时绑定的最小端口数
  /// 若为0则绑定随机数量的端口
  /// The lowest port number that workers started will bind on.
  /// If this is set to 0, workers will bind on random ports.
  int min_worker_port;
  /// workers启动时绑定的最大端口数
  /// 若没有设置为0，则min_worker_port也必须不为0
  /// The highest port number that workers started will bind on.
  /// If this is not set to 0, min_worker_port must also not be set to 0.
  int max_worker_port;
  /// 一个关于wokers启动时绑定的开放端口 explicit list 表
  /// 这优先于 min_worker_port 和 max_worker_port.
  /// An explicit list of open ports that workers started will bind
  /// on. This takes precedence over min_worker_port and max_worker_port.
  std::vector<int> worker_ports;
  /// 对于workers的软限制数
  /// The soft limit of the number of workers.
  int num_workers_soft_limit;
  /// 用于第一个任务的Python workers的初始数量
  /// Number of initial Python workers for the first job.
  int num_initial_python_workers_for_first_job;
  /// worker池同时启动的worker的最大数量
  /// The maximum number of workers that can be started concurrently by a
  /// worker pool.
  int maximum_startup_concurrency;
  /// 用于启动worker进程的命令们，按语言分类
  /// The commands used to start the worker process, grouped by language.
  WorkerCommandMap worker_commands;
  /// 用于启动agent的命令
  /// The command used to start agent.
  std::string agent_command;
  /// 报告资源的时间间隔（以毫秒为单位）
  /// The time between reports resources in milliseconds.
  uint64_t report_resources_period_ms;
  /// 存储的套接字名称
  /// The store socket name.
  std::string store_socket_name;
  /// ray 临时路径地址
  /// The path to the ray temp dir.
  std::string temp_dir;
  /// 该ray 会话路径
  /// The path of this ray session dir.
  std::string session_dir;
  /// 该ray的资源路径
  /// The path of this ray resource dir.
  std::string resource_dir;
  /// 若为真，则ray 调试器在外部可用
  /// If true make Ray debugger available externally.
  int ray_debugger_external;
  /// 该节点的raylet配置表
  /// The raylet config list of this node.
  std::string raylet_config;
  // 记录指标之间的时间间隔（以毫秒为单位）
  // 若为0，则不可用
  // The time between record metrics in milliseconds, or 0 to disable.
  uint64_t record_metrics_period_ms;
  //最大的io workers数量（原注释number if max io，怀疑是打错了）
  // The number if max io workers.
  int max_io_workers;
  //每一个spill 操作泄露的最小对象大小
  // The minimum object size that can be spilled by each spill operation.
  int64_t min_spilling_size;
};

class HeartbeatSender {
 public:
  /// 创建一个心跳发送器
  /// Create a heartbeat sender.
  ///
  /// 参数 seld_node_id 该节点的ID
  /// 参数 gcs_client 发送心跳到哪个GCS客户端
  /// \param self_node_id ID of this node.
  /// \param gcs_client GCS client to send heartbeat.
  HeartbeatSender(NodeID self_node_id, std::shared_ptr<gcs::GcsClient> gcs_client);

  ~HeartbeatSender();

 private:
  /// 向GCS发送心跳
  /// Send heartbeats to the GCS.
  void Heartbeat();

  /// 该节点的id
  /// ID of this node.
  NodeID self_node_id_;
  /// 一个连结GCS的客户端连结
  /// A client connection to the GCS.
  std::shared_ptr<gcs::GcsClient> gcs_client_;
  /// 心跳循环中使用的io服务，防止他被主进程阻塞
  /// The io service used in heartbeat loop in case of it being
  /// blocked by main thread.
  instrumented_io_context heartbeat_io_service_;
  /// geartbeat_io_service_所使用的心跳线程
  /// Heartbeat thread, using with heartbeat_io_service_.
  std::unique_ptr<std::thread> heartbeat_thread_;
  std::unique_ptr<PeriodicalRunner> heartbeat_runner_;
  /// 最后一次心跳的发送时间，用来确保我们与心跳保持联系
  /// The time that the last heartbeat was sent at. Used to make sure we are
  /// keeping up with heartbeats.
  uint64_t last_heartbeat_at_ms_;
};

class NodeManager : public rpc::NodeManagerServiceHandler {
 public:
  /// 创建一个节点管理类 
  /// Create a node manager.
  ///
  /// 参数 resource_config 节点资源的初始设置
  /// 参数 object_manager 本地对象管理（local object manager）的一个引用
  /// \param resource_config The initial set of node resources.
  /// \param object_manager A reference to the local object manager.
  NodeManager(instrumented_io_context &io_service, const NodeID &self_node_id,
              const NodeManagerConfig &config,
              const ObjectManagerConfig &object_manager_config,
              std::shared_ptr<gcs::GcsClient> gcs_client);
  /// 处理一个新的客户端连结
  /// Process a new client connection.
  ///
  /// 参数 client 要处理的客户端
  /// 返回 空
  /// \param client The client to process.
  /// \return Void.
  void ProcessNewClient(ClientConnection &client);

  /// 处理来自一个客户端信息。
  /// 这个方法负责当客户端存活时继续监听从而获得更多信息
  /// 
  /// 参数 client 发送信息的客户端
  /// 参数 message_type 信息类型（例如一个 flatbuffer enum）
  /// 参数 message_data 一个指向消息数据的指针
  /// 返回 空

  /// Process a message from a client. This method is responsible for
  /// explicitly listening for more messages from the client if the client is
  /// still alive.
  ///
  /// \param client The client that sent the message.
  /// \param message_type The message type (e.g., a flatbuffer enum).
  /// \param message_data A pointer to the message data.
  /// \return Void.
  void ProcessClientMessage(const std::shared_ptr<ClientConnection> &client,
                            int64_t message_type, const uint8_t *message_data);

  /// 在相关的GCS表上预定并设置处理器
  /// 返回
  /// Subscribe to the relevant GCS tables and set up handlers.
  /// 返回 一个显示操作是否成功的状态
  /// \return Status indicating whether this was done successfully or not.
  ray::Status RegisterGcs();

  /// 获得初始节点管理配置
  /// Get initial node manager configuration.
  const NodeManagerConfig &GetInitialConfig() const;

  /// 返回类的debug 字符串
  /// 返回 string
  /// Returns debug string for class.
  ///
  /// \return string.
  std::string DebugString() const;

  /// 记录衡量指标
  /// Record metrics.
  void RecordMetrics();

  /// 获得节点管理的rpc服务端口
  /// Get the port of the node manager rpc server.
  int GetServerPort() const { return node_manager_server_.GetPort(); }

  int GetObjectManagerPort() const { return object_manager_.GetServerPort(); }

  LocalObjectManager &GetLocalObjectManager() { return local_object_manager_; }

  /// 通过集群触发全局GC来释放到actors的接口或object的ids
  /// Trigger global GC across the cluster to free up references to actors or
  /// object ids.
  void TriggerGlobalGC();

  /// 用给定的错误类型标记失败的指定对象
  /// 参数 error_type 导致任务失败的错误类型
  /// 参数 object_ids 用于存储错误信息的对象的ids
  /// 参数 job_id 用来push写操作是否失败的optional job
  /// Mark the specified objects as failed with the given error type.
  ///
  /// \param error_type The type of the error that caused this task to fail.
  /// \param object_ids The object ids to store error messages into.
  /// \param job_id The optional job to push errors to if the writes fail.
  void MarkObjectsAsFailed(const ErrorType &error_type,
                           const std::vector<rpc::ObjectReference> object_ids,
                           const JobID &job_id);
  /// 停止该节点管理器
  /// Stop this node manager.
  void Stop();

 private:
  /// 用于处理节点们的方法们
  /// Methods for handling nodes.

  /// 处理来自GCS 订阅发布的未预料的失败提醒
  ///
  /// 参数 data 死掉的worker的数据
  /// Handle an unexpected failure notification from GCS pubsub.
  ///
  /// \param data The data of the worker that died.
  void HandleUnexpectedWorkerFailure(const rpc::WorkerDeltaData &data);

  /// 用于处理新节点添加的handler
  ///
  /// 参数 data 与新节点有关的数据
  /// 返回 空
  /// Handler for the addition of a new node.
  ///
  /// \param data Data associated with the new node.
  /// \return Void.
  void NodeAdded(const GcsNodeInfo &data);
  /// 移除一个GCS节点的handler
  /// 参数 node_id 移除节点的编号
  /// 返回 空
  /// Handler for the removal of a GCS node.
  /// \param node_id Id of the removed node.
  /// \return Void.
  void NodeRemoved(const NodeID &node_id);

  /// 用来增加或更新GCS中的资源的handler
  /// 参数 node_id 创建或者更新资源的节点id
  /// 参数 createUpdatedResources 创建或更新的资源
  /// 返回 空
  /// Handler for the addition or updation of a resource in the GCS
  /// \param node_id ID of the node that created or updated resources.
  /// \param createUpdatedResources Created or updated resources.
  /// \return Void.
  void ResourceCreateUpdated(const NodeID &node_id,
                             const ResourceSet &createUpdatedResources);

  /// 用来删除GCS中的资源的handler
  /// 参数 node_id 删除资源的节点id
  /// 参数 resource_names 删除的资源的名称
  /// 返回 空

  /// Handler for the deletion of a resource in the GCS
  /// \param node_id ID of the node that deleted resources.
  /// \param resource_names Names of deleted resources.
  /// \return Void.
  void ResourceDeleted(const NodeID &node_id,
                       const std::vector<std::string> &resource_names);

  /// 评估本地的无法执行队列来检查是否有任务可以被安排
  /// 该方法将会当本地节点资源有任何更新时被调用
  /// 返回 空

  /// Evaluates the local infeasible queue to check if any tasks can be scheduled.
  /// This is called whenever there's an update to the resources on the local node.
  /// \return Void.
  void TryLocalInfeasibleTaskScheduling();

  /// 填写通常任务资源报告
  /// Fill out the normal task resource report.
  void FillNormalTaskResourceUsage(rpc::ResourcesData &resources_data);

  /// 填写资源报告。该方法可以被任意向GCS传递报告的方法调用
  /// Fill out the resource report. This can be called by either method to transport the
  /// report to GCS.
  void FillResourceReport(rpc::ResourcesData &resources_data);

  /// 用一个文件存储调试状态
  /// Write out debug state to a file.
  void DumpDebugState() const;

  /// 刷新应用程序中超出范围的对象，这将尝试从集群中驱逐所有的plasma副本
  /// Flush objects that are out of scope in the application. This will attempt
  /// to eagerly evict all plasma copies of the object from the cluster.
  void FlushObjectsToFree();

  /// 用来处理来自GCS的资源利用率通知的handler
  ///
  /// 参数 id 发送资源数据的节点id
  /// 参数 data 包括加载信息的资源数据
  /// 返回 空
  /// Handler for a resource usage notification from the GCS.
  ///
  /// \param id The ID of the node manager that sent the resources data.
  /// \param data The resources data including load information.
  /// \return Void.
  void UpdateResourceUsage(const NodeID &id, const rpc::ResourcesData &data);
  /// 用来获取来自GCS的批（batch)资源处理通知
  /// 参数 resource_usage_batch 资源利用率数据的batch
  /// Handler for a resource usage batch notification from the GCS
  ///
  /// \param resource_usage_batch The batch of resource usage data.
  void ResourceUsageBatchReceived(const ResourceUsageBatchData &resource_usage_batch);

  /// 处理一个完成了被分配的任务的worker
  /// 参数 worker 完成任务的worker
  /// 返回 这个worker是否应该被返回到工厂池子中，只有当直接的actor创建调用时才为false
  /// Handle a worker finishing its assigned task.
  ///
  /// \param worker The worker that finished the task.
  /// \return Whether the worker should be returned to the idle pool. This is
  /// only false for direct actor creation calls, which should never be
  /// returned to idle.
  bool FinishAssignedTask(const std::shared_ptr<WorkerInterface> &worker_ptr);
  /// 为一个新创建的actor产生actor表数据的助手
  ///
  /// 参数 task_spec 创建这个actor的actor创建任务的RayTask规范
  /// 参数 worker 这个actor监听的端口
  /// Helper function to produce actor table data for a newly created actor.
  ///
  /// \param task_spec RayTask specification of the actor creation task that created the
  /// actor.
  /// \param worker The port that the actor is listening on.
  std::shared_ptr<ActorTableData> CreateActorTableDataFromCreationTask(
      const TaskSpecification &task_spec, int port, const WorkerID &worker_id);

  /// 处理一个完成了分配的创建actor任务的worker
  /// 参数 worker 完成这个任务的woker 
  /// 参数 task actor任务或者创建actor的任务
  /// 返回 空
  /// Handle a worker finishing an assigned actor creation task.
  /// \param worker The worker that finished the task.
  /// \param task The actor task or actor creation task.
  /// \return Void.
  void FinishAssignedActorCreationTask(WorkerInterface &worker, const RayTask &task);

  /// 处理获取对象的阻塞（blocking）。这可能作为一个任务分配给一个worker，一个OOB任务
  /// （例如一个被应用创建的现成），或者一个driver任务。
  /// 当客户端开始get call或者wait call时这个方法会被触发
  ///
  /// 参数 client 执行这个阻塞任务的客户端
  /// 参数 required_object_refs 这个客户端所等待的对象
  /// 参数 current_task_id 被阻塞的任务
  /// 参数 ray_get 这个任务是否被阻塞在一个"ray.get"调用里
  /// 参数 mark_worker_blocked 是否把这个worker标记为阻塞，当直接调用时，应为false
  /// 返回 空
  
  /// Handle blocking gets of objects. This could be a task assigned to a worker,
  /// an out-of-band task (e.g., a thread created by the application), or a
  /// driver task. This can be triggered when a client starts a get call or a
  /// wait call.
  ///
  /// \param client The client that is executing the blocked task.
  /// \param required_object_refs The objects that the client is blocked waiting for.
  /// \param current_task_id The task that is blocked.
  /// \param ray_get Whether the task is blocked in a `ray.get` call.
  /// \param mark_worker_blocked Whether to mark the worker as blocked. This
  ///                            should be False for direct calls.
  /// \return Void.
  void AsyncResolveObjects(const std::shared_ptr<ClientConnection> &client,
                           const std::vector<rpc::ObjectReference> &required_object_refs,
                           const TaskID &current_task_id, bool ray_get,
                           bool mark_worker_blocked);

  /// 处理一个获取对象的阻塞的结束。这可能是一个分配给worker的任务，一个OOB任务
  /// 或者一个driver任务。当一个客户端完成get call或者wait call时会触发这个函数。
  /// 给与的任务必须是被之前的调用的AsyncResolveObjects()函数所阻塞的任务
  /// 参数 client 执行这个解除阻塞任务的客户端
  /// 参数 current_task_id 解除阻塞的任务
  /// 参数 worker_was_blocked 我们之前是否用AsyncResolveObjects()函数标记worker为阻塞
  /// 返回 空
  /// Handle end of a blocking object get. This could be a task assigned to a
  /// worker, an out-of-band task (e.g., a thread created by the application),
  /// or a driver task. This can be triggered when a client finishes a get call
  /// or a wait call. The given task must be blocked, via a previous call to
  /// AsyncResolveObjects.
  ///
  /// \param client The client that is executing the unblocked task.
  /// \param current_task_id The task that is unblocked.
  /// \param worker_was_blocked Whether we previously marked the worker as
  ///                           blocked in AsyncResolveObjects().
  /// \return Void.
  void AsyncResolveObjectsFinish(const std::shared_ptr<ClientConnection> &client,
                                 const TaskID &current_task_id, bool was_blocked);
  /// 处理直接的被阻塞的任务调用。注意，回调可能在woker租赁已经返回给节点管理器才抵达
  /// 参数 worker   分享ptr的worker,丢失时为nullptr
  ///
  /// Handle a direct call task that is blocked. Note that this callback may
  /// arrive after the worker lease has been returned to the node manager.
  ///
  /// \param worker Shared ptr to the worker, or nullptr if lost.
  void HandleDirectCallTaskBlocked(const std::shared_ptr<WorkerInterface> &worker,
                                   bool release_resources);
  /// 处理直接的解除阻塞的任务调用。注意，回调可能在woker租赁已经返回给节点管理器才抵达
  /// 但是，他一定在DirectCallTaskBlocked()之后到达
  /// 参数 worker   分享ptr的worker,丢失时为nullptr
  /// 
  /// Handle a direct call task that is unblocked. Note that this callback may
  /// arrive after the worker lease has been returned to the node manager.
  /// However, it is guaranteed to arrive after DirectCallTaskBlocked.
  ///
  /// \param worker Shared ptr to the worker, or nullptr if lost.
  void HandleDirectCallTaskUnblocked(const std::shared_ptr<WorkerInterface> &worker);

  /// 杀死一个woker
  ///
  /// 参数 worker 要杀死的worker
  /// 返回 空
  /// Kill a worker.
  ///
  /// \param worker The worker to kill.
  /// \return Void.
  void KillWorker(std::shared_ptr<WorkerInterface> worker);

  /// 摧毁一个woeker
  /// 我们会先断开一个worker的链接，然后杀死这个worker
  /// 参数 woker 操作对象
  /// 返回 空
  ///
  /// Destroy a worker.
  /// We will disconnect the worker connection first and then kill the worker.
  ///
  /// \param worker The worker to destroy.
  /// \return Void.
  void DestroyWorker(
      std::shared_ptr<WorkerInterface> worker,
      rpc::WorkerExitType disconnect_type = rpc::WorkerExitType::SYSTEM_ERROR_EXIT);
  /// 当一个工作完成时，循环遍历队列中的所有该工作的所有任务，并将它们标记为失败
  ///
  /// 参数 job_id 退出的工作
  /// 返回 空
  /// When a job finished, loop over all of the queued tasks for that job and
  /// treat them as failed.
  ///
  /// \param job_id The job that exited.
  /// \return Void.
  void CleanUpTasksForFinishedJob(const JobID &job_id);

  /// 处理一个变成本地对象的对象。这将会更新所有的本地账户(accounting)
  /// 但是并不会写入到任何GCS上的全局账户
  /// 
  /// 参数 object_info 本地能够获取的对象的信息
  /// 返回 空
  /// Handle an object becoming local. This updates any local accounting, but
  /// does not write to any global accounting in the GCS.
  ///
  /// \param object_info The info about the object that is locally available.
  /// \return Void.
  void HandleObjectLocal(const ObjectInfo &object_info);
  /// 处理已经不在本地的对象，会更新所有本地账户，但是不写入任何GCS上的全局账户
  /// 参数 object_id 不在本地的对象id
  /// 返回 空

  /// Handle an object that is no longer local. This updates any local
  /// accounting, but does not write to any global accounting in the GCS.
  ///
  /// \param object_id The object that has been evicted locally.
  /// \return Void.
  void HandleObjectMissing(const ObjectID &object_id);

  /// 处理工作开始事件
  ///
  /// 参数 job_id 启动的工作的编号
  /// 参数 job_data 与启动的工作关联的数据
  /// 返回
  /// Handles the event that a job is started.
  ///
  /// \param job_id ID of the started job.
  /// \param job_data Data associated with the started job.
  /// \return Void
  void HandleJobStarted(const JobID &job_id, const JobTableData &job_data);

  /// 处理工作结束事件
  ///
  /// 参数 job_id 结束的工作的编号
  /// 参数 job_data 与结束的工作关联的数据
  /// 返回 空
  /// Handles the event that a job is finished.
  ///
  /// \param job_id ID of the finished job.
  /// \param job_data Data associated with the finished job.
  /// \return Void.
  void HandleJobFinished(const JobID &job_id, const JobTableData &job_data);

  /// 处理 NotifyDirectCallTaskBlocked的客户端消息
  /// 参数 message_data 指向消息数据的指针
  /// 返回 空
  /// Process client message of NotifyDirectCallTaskBlocked
  ///
  /// \param message_data A pointer to the message data.
  /// \return Void.
  void ProcessDirectCallTaskBlocked(const std::shared_ptr<ClientConnection> &client,
                                    const uint8_t *message_data);

  /// 处理RegisterClientRequest的客户端消息
  /// 参数 client 发送这个信息的客户端
  /// 参数 message_data 一个指向消息数据的指针
  /// 返回 空
  ///
  /// Process client message of RegisterClientRequest
  ///
  /// \param client The client that sent the message.
  /// \param message_data A pointer to the message data.
  /// \return Void.
  void ProcessRegisterClientRequestMessage(
      const std::shared_ptr<ClientConnection> &client, const uint8_t *message_data);
  /// 处理AnnounceWorkerPort的客户端消息
  /// 参数 client 发送这个信息的客户端
  /// 参数 message_data 一个指向消息数据的指针
  /// 返回 空
  ///
  /// Process client message of AnnounceWorkerPort
  ///
  /// \param client The client that sent the message.
  /// \param message_data A pointer to the message data.
  /// \return Void.
  void ProcessAnnounceWorkerPortMessage(const std::shared_ptr<ClientConnection> &client,
                                        const uint8_t *message_data);
  /// 处理worker可用的情况
  /// 
  ///参数 client 这个worker的连结
  ///返回 空
  
  /// Handle the case that a worker is available.
  ///
  /// \param client The connection for the worker.
  /// \return Void.
  void HandleWorkerAvailable(const std::shared_ptr<ClientConnection> &client);

  /// 处理worker可用的情况
  /// 
  ///参数 worker 这个worker的指针
  ///返回 空

  /// Handle the case that a worker is available.
  ///
  /// \param worker The pointer to the worker
  /// \return Void.
  void HandleWorkerAvailable(const std::shared_ptr<WorkerInterface> &worker);

  /// 处理失去连结的客户端，这个函数可被一个客户端调用多次，因为当客户端失去连结和
  /// 节点管理器向该客户端写入信息失败时都可以触发
  /// 参数 client 发送这个消息的客户端
  /// 参数 message_date 消息数据的指针
  /// 返回 空
  /// Handle a client that has disconnected. This can be called multiple times
  /// on the same client because this is triggered both when a client
  /// disconnects and when the node manager fails to write a message to the
  /// client.
  ///
  /// \param client The client that sent the message.
  /// \param message_data A pointer to the message data.
  /// \return Void.
  void ProcessDisconnectClientMessage(const std::shared_ptr<ClientConnection> &client,
                                      const uint8_t *message_data);
  /// 处理客户端FetchOrReconstruct的消息
  /// 参数 client 发送消息的客户端
  /// 参数 message_data 指向消息数据的指针
  /// 返回 空
  /// Process client message of FetchOrReconstruct
  ///
  /// \param client The client that sent the message.
  /// \param message_data A pointer to the message data.
  /// \return Void.
  void ProcessFetchOrReconstructMessage(const std::shared_ptr<ClientConnection> &client,
                                        const uint8_t *message_data);

  /// 处理客户端WaitRequest的消息
  /// 参数 client 发送消息的客户端
  /// 参数 message_data 指向消息数据的指针
  /// 返回 空
  /// Process client message of WaitRequest
  ///
  /// \param client The client that sent the message.
  /// \param message_data A pointer to the message data.
  /// \return Void.
  void ProcessWaitRequestMessage(const std::shared_ptr<ClientConnection> &client,
                                 const uint8_t *message_data);
  /// 处理客户端WaitForDirectActorCallArgsRequest的消息
  /// 参数 client 发送消息的客户端
  /// 参数 message_data 指向消息数据的指针
  /// 返回 空
  /// Process client message of WaitForDirectActorCallArgsRequest
  ///
  /// \param client The client that sent the message.
  /// \param message_data A pointer to the message data.
  /// \return Void.
  void ProcessWaitForDirectActorCallArgsRequestMessage(
      const std::shared_ptr<ClientConnection> &client, const uint8_t *message_data);
  /// 处理客户端PushErrorRequest的消息
  /// 参数 message_data 指向消息数据的指针
  /// 返回 空
  /// Process client message of PushErrorRequest
  ///
  /// \param message_data A pointer to the message data.
  /// \return Void.
  void ProcessPushErrorRequestMessage(const uint8_t *message_data);

  /// 让处理worker预定给一个给定的plasma对象变得可行。这个handler保证plasma对象是本地的
  /// 并且调用core worker的PlasmaObjectReady gRPC 端点
  /// 
  /// 参数 client 发送这条信息的客户端
  /// 参数 message_data 指向消息数据的指针
  /// 返回 空
  /// Process worker subscribing to a given plasma object become available. This handler
  /// makes sure that the plasma object is local and calls core worker's PlasmaObjectReady
  /// gRPC endpoint.
  ///
  /// \param client The client that sent the message.
  /// \param message_data A pointer to the message data.
  /// \return void.
  void ProcessSubscribePlasmaReady(const std::shared_ptr<ClientConnection> &client,
                                   const uint8_t *message_data);

  void HandleUpdateResourceUsage(const rpc::UpdateResourceUsageRequest &request,
                                 rpc::UpdateResourceUsageReply *reply,
                                 rpc::SendReplyCallback send_reply_callback) override;

  /// Handle a `RequestResourceReport` request.
  void HandleRequestResourceReport(const rpc::RequestResourceReportRequest &request,
                                   rpc::RequestResourceReportReply *reply,
                                   rpc::SendReplyCallback send_reply_callback) override;

  /// Handle a `PrepareBundleResources` request.
  void HandlePrepareBundleResources(const rpc::PrepareBundleResourcesRequest &request,
                                    rpc::PrepareBundleResourcesReply *reply,
                                    rpc::SendReplyCallback send_reply_callback) override;

  /// Handle a `CommitBundleResources` request.
  void HandleCommitBundleResources(const rpc::CommitBundleResourcesRequest &request,
                                   rpc::CommitBundleResourcesReply *reply,
                                   rpc::SendReplyCallback send_reply_callback) override;

  /// Handle a `ResourcesReturn` request.
  void HandleCancelResourceReserve(const rpc::CancelResourceReserveRequest &request,
                                   rpc::CancelResourceReserveReply *reply,
                                   rpc::SendReplyCallback send_reply_callback) override;

  /// Handle a `WorkerLease` request.
  void HandleRequestWorkerLease(const rpc::RequestWorkerLeaseRequest &request,
                                rpc::RequestWorkerLeaseReply *reply,
                                rpc::SendReplyCallback send_reply_callback) override;

  /// Handle a `ReturnWorker` request.
  void HandleReturnWorker(const rpc::ReturnWorkerRequest &request,
                          rpc::ReturnWorkerReply *reply,
                          rpc::SendReplyCallback send_reply_callback) override;

  /// Handle a `ReleaseUnusedWorkers` request.
  void HandleReleaseUnusedWorkers(const rpc::ReleaseUnusedWorkersRequest &request,
                                  rpc::ReleaseUnusedWorkersReply *reply,
                                  rpc::SendReplyCallback send_reply_callback) override;

  /// Handle a `ReturnWorker` request.
  void HandleCancelWorkerLease(const rpc::CancelWorkerLeaseRequest &request,
                               rpc::CancelWorkerLeaseReply *reply,
                               rpc::SendReplyCallback send_reply_callback) override;

  /// Handle a `PinObjectIDs` request.
  void HandlePinObjectIDs(const rpc::PinObjectIDsRequest &request,
                          rpc::PinObjectIDsReply *reply,
                          rpc::SendReplyCallback send_reply_callback) override;

  /// Handle a `NodeStats` request.
  void HandleGetNodeStats(const rpc::GetNodeStatsRequest &request,
                          rpc::GetNodeStatsReply *reply,
                          rpc::SendReplyCallback send_reply_callback) override;

  /// Handle a `GlobalGC` request.
  void HandleGlobalGC(const rpc::GlobalGCRequest &request, rpc::GlobalGCReply *reply,
                      rpc::SendReplyCallback send_reply_callback) override;

  /// Handle a `FormatGlobalMemoryInfo`` request.
  void HandleFormatGlobalMemoryInfo(const rpc::FormatGlobalMemoryInfoRequest &request,
                                    rpc::FormatGlobalMemoryInfoReply *reply,
                                    rpc::SendReplyCallback send_reply_callback) override;

  /// Handle a `RequestObjectSpillage` request.
  void HandleRequestObjectSpillage(const rpc::RequestObjectSpillageRequest &request,
                                   rpc::RequestObjectSpillageReply *reply,
                                   rpc::SendReplyCallback send_reply_callback) override;

  /// Handle a `ReleaseUnusedBundles` request.
  void HandleReleaseUnusedBundles(const rpc::ReleaseUnusedBundlesRequest &request,
                                  rpc::ReleaseUnusedBundlesReply *reply,
                                  rpc::SendReplyCallback send_reply_callback) override;

  /// Handle a `GetSystemConfig` request.
  void HandleGetSystemConfig(const rpc::GetSystemConfigRequest &request,
                             rpc::GetSystemConfigReply *reply,
                             rpc::SendReplyCallback send_reply_callback) override;

  /// Handle a `GetGcsServerAddress` request.
  void HandleGetGcsServerAddress(const rpc::GetGcsServerAddressRequest &request,
                                 rpc::GetGcsServerAddressReply *reply,
                                 rpc::SendReplyCallback send_reply_callback) override;

  /// 在该raylet的每一个worker上触发本地GC
  /// Trigger local GC on each worker of this raylet.
  void DoLocalGC();

  /// 发送error到driver，当我们不能安排新的任务或者actor例如节点的actor已经满了时
  /// Push an error to the driver if this node is full of actors and so we are
  /// unable to schedule new tasks or actors at all.
  void WarnResourceDeadlock();

  /// 将任务分配给可用的worker
  /// Dispatch tasks to available workers.
  void DispatchScheduledTasksToWorkers();

  /// 一个任务是否是由actor创建的
  /// Whether a task is an actor creation task.
  bool IsActorCreationTask(const TaskID &task_id);

  /// 返回所有的bundle资源
  /// 参数 bundle_spec 说明谁将被返回的bundle规范
  /// 返回 资源是否成功返回
  ///
  /// Return back all the bundle resource.
  ///
  /// \param bundle_spec: Specification of bundle whose resources will be returned.
  /// \return Whether the resource is returned successfully.
  bool ReturnBundleResources(const BundleSpecification &bundle_spec);

  /// 将无法执行的任务错误送到GCS以便driver查阅并打印该任务
  /// 参数 task 不可行的RayTask
  /// 
  /// Publish the infeasible task error to GCS so that drivers can subscribe to it and
  /// print.
  ///
  /// \param task RayTask that is infeasible
  void PublishInfeasibleTaskError(const RayTask &task) const;

  /// 获得储存在plasma中的对象的指针
  /// 一旦返回的引用超出范围，他们就会被同时释放
  /// 参数[in] object_ids 要获得的对象
  /// 参数[our] results 存储在plasma中的对象指针
  /// 返回 请求是否成功
  ///
  /// Get pointers to objects stored in plasma. They will be
  /// released once the returned references go out of scope.
  ///
  /// \param[in] object_ids The objects to get.
  /// \param[out] results The pointers to objects stored in
  /// plasma.
  /// \return Whether the request was successful.
  bool GetObjectsFromPlasma(const std::vector<ObjectID> &object_ids,
                            std::vector<std::unique_ptr<RayObject>> *results);
  /// 填充心跳表的关联部分，用于发送raylet和gcs heartbeats之间的心跳，应该填入
  /// resource_load和resource_load_by_shape
  ///
  /// 参数 输出参数 只使用"resource_load"和"resource_load_by_shape"两个文件
  ///
  /// Populate the relevant parts of the heartbeat table. This is intended for
  /// sending raylet <-> gcs heartbeats. In particular, this should fill in
  /// resource_load and resource_load_by_shape.
  ///
  /// \param Output parameter. `resource_load` and `resource_load_by_shape` are the only
  /// fields used.
  void FillResourceUsage(rpc::ResourcesData &data);

  /// 断开一个客户端的连接
  /// 参数 client 发送该信息的客户端
  /// 参数 disconnect_type 客户端断开连接的原因
  /// 参数 client_error_message 关于这次断开连接的额外错误信息
  /// 返回 空
  /// Disconnect a client.
  ///
  /// \param client The client that sent the message.
  /// \param disconnect_type The reason to disconnect the specified client.
  /// \param client_error_message Extra error messages about this disconnection
  /// \return Void.
  void DisconnectClient(
      const std::shared_ptr<ClientConnection> &client,
      rpc::WorkerExitType disconnect_type = rpc::WorkerExitType::SYSTEM_ERROR_EXIT,
      const std::shared_ptr<rpc::RayException> &creation_task_exception = nullptr);

  /// 删除本地节点上的URI
  /// 参数 uri 资源的URI
  /// Delete URI in local node.
  ///
  /// \param uri The URI of the resource.
  void DeleteLocalURI(const std::string &uri, std::function<void(bool)> cb);

  /// 节点id
  /// ID of this node.
  NodeID self_node_id_;
  instrumented_io_context &io_service_;
  /// 连接到GCS的客户端
  /// A client connection to the GCS.
  std::shared_ptr<gcs::GcsClient> gcs_client_;
  /// 向GCS发送心跳的类
  /// Class to send heartbeat to GCS.
  std::unique_ptr<HeartbeatSender> heartbeat_sender_;
  ///worker的池子
  /// A pool of workers.
  WorkerPool worker_pool_;
  /// 被所有"NodeManagerClient"和"CoreWorkerClient"共享的"ClientCallManager"
  /// The `ClientCallManager` object that is shared by all `NodeManagerClient`s
  /// as well as all `CoreWorkerClient`s.
  rpc::ClientCallManager client_call_manager_;
  /// 连接到core worker的RPC客户端池
  /// Pool of RPC client connections to core workers.
  rpc::CoreWorkerClientPool worker_rpc_pool_;
  /// 向core worker发起 pubsub的raylet客户端
  /// 用来 确定要去除的对象
  /// The raylet client to initiate the pubsub to core workers (owners).
  /// It is used to subscribe objects to evict.
  std::unique_ptr<pubsub::SubscriberInterface> core_worker_subscriber_;
  /// 对象表，在object manager和node manager间共享
  /// The object table. This is shared between the object manager and node
  /// manager.
  std::unique_ptr<IObjectDirectory> object_directory_;
  /// 管理客户端对对象传输和可用性的请求
  /// Manages client requests for object transfers and availability.
  ObjectManager object_manager_;
  /// Plasma对象存储客户端，用于在对象存储中创建新的对象（例如，为因为actor死亡而无法运行的
  /// actor 任务)并且锁定集群中在范围内的对象
  /// A Plasma object store client. This is used for creating new objects in
  /// the object store (e.g., for actor tasks that can't be run because the
  /// actor died) and to pin objects that are in scope in the cluster.
  plasma::PlasmaClient store_client_;
  /// 定期运行function的runner
  /// The runner to run function periodically.
  PeriodicalRunner periodical_runner_;
  /// 资源报告计时器的时间段
  /// The period used for the resources report timer.
  uint64_t report_resources_period_ms_;
  /// 上一次资源报告的发送时间，用来确保我们和资源报告器保持联系
  /// The time that the last resource report was sent at. Used to make sure we are
  /// keeping up with resource reports.
  uint64_t last_resource_report_at_ms_;
  /// 每次遇到潜在的资源死锁时递增
  /// 当情况清除时重置为0
  /// Incremented each time we encounter a potential resource deadlock condition.
  /// This is reset to zero when the condition is cleared.
  int resource_deadlock_warned_ = 0;
  /// 我们是否记录了任何指标
  /// Whether we have recorded any metrics yet.
  bool recorded_metrics_ = false;
  /// ray temp位置的路径
  /// The path to the ray temp dir.
  std::string temp_dir_;
  /// 最初的node manager配置
  /// Initial node manager configuration.
  const NodeManagerConfig initial_config_;

  /// 一个用来解决叫做队列中的叫做"ray.get"或"ray.wait"的任务和worker需要的对象的管理器
  /// A manager to resolve objects needed by queued tasks and workers that
  /// called `ray.get` or `ray.wait`.
  DependencyManager dependency_manager_;

  std::shared_ptr<AgentManager> agent_manager_;

  /// RPC服务器
  /// The RPC server.
  rpc::GrpcServer node_manager_server_;

  /// 节点管理的RPC服务
  /// The node manager RPC service.
  rpc::NodeManagerGrpcService node_manager_service_;

  /// 代理管理器的RPC服务
  /// The agent manager RPC service.
  std::unique_ptr<rpc::AgentManagerServiceHandler> agent_manager_service_handler_;
  rpc::AgentManagerGrpcService agent_manager_service_;

  /// 管理锁定（主副本）、释放和/或溢出的所有本地对象。
  /// Manages all local objects that are pinned (primary
  /// copies), freed, and/or spilled.
  LocalObjectManager local_object_manager_;

  /// 从节点ID映射到远程节点管理器的地址的map
  /// Map from node ids to addresses of the remote node managers.
  absl::flat_hash_map<NodeID, std::pair<std::string, int32_t>>
      remote_node_manager_addresses_;

  /// 租借给直接调用客户端的worker的map
  /// Map of workers leased out to direct call clients.
  std::unordered_map<WorkerID, std::shared_ptr<WorkerInterface>> leased_workers_;

  /// 从所有者workerID映射到所有者租用的workerID列表的map
  /// Map from owner worker ID to a list of worker IDs that the owner has a
  /// lease on.
  absl::flat_hash_map<WorkerID, std::vector<WorkerID>> leased_workers_by_owner_;

  /// 在下一个资源利用率报告中是否触发全局GC。这将广播一个全局GC消息到除了自己的所有的raylets
  /// Whether to trigger global GC in the next resource usage report. This will broadcast
  /// a global GC message to all raylets except for this one.
  bool should_global_gc_ = false;

  /// 在下一个资源利用率报告中是否触发本地GC。这将在该raylet上所有的本地worker上触发
  /// Whether to trigger local GC in the next resource usage report. This will trigger gc
  /// on all local workers of this raylet.
  bool should_local_gc_ = false;

  /// 当plasma存储利用率高时，将会运行gc来降低他
  /// When plasma storage usage is high, we'll run gc to reduce it.
  double high_plasma_storage_usage_ = 1.0;

  /// 本地gc运行的时间戳
  /// the timestampe local gc run
  uint64_t local_gc_run_time_ns_;

  /// 本地gc的节流器
  /// Throttler for local gc
  Throttler local_gc_throttler_;

  /// 全局gc的节流器
  /// Throttler for global gc
  Throttler global_gc_throttler_;

  /// 初始化本地gc的时间
  /// Seconds to initialize a local gc
  const uint64_t local_gc_interval_ns_;

  /// 这两个类构成了新的调度程序。
  /// ClusterResourceScheduler负责维护集群状态，例如资源使用状况的视图
  /// ClusterTaskManager负责排列，返回和分派任务
  /// 
  /// These two classes make up the new scheduler. ClusterResourceScheduler is
  /// responsible for maintaining a view of the cluster state w.r.t resource
  /// usage. ClusterTaskManager is responsible for queuing, spilling back, and
  /// dispatching tasks.
  std::shared_ptr<ClusterResourceSchedulerInterface> cluster_resource_scheduler_;
  std::shared_ptr<ClusterTaskManagerInterface> cluster_task_manager_;

  absl::flat_hash_map<ObjectID, std::unique_ptr<RayObject>> pinned_objects_;

  /// TODO(swang):从缓存中去除这些条目
  /// GCS中的WorkerTable表的缓存
  /// TODO(swang): Evict entries from these caches.
  /// Cache for the WorkerTable in the GCS.
  absl::flat_hash_set<WorkerID> failed_workers_cache_;
  /// GCS中NodeTable表的缓存
  /// Cache for the NodeTable in the GCS.
  absl::flat_hash_set<NodeID> failed_nodes_cache_;

  /// 以下映射的并发性
  /// Concurrency for the following map
  mutable absl::Mutex plasma_object_notification_lock_;

  /// 跟踪等待对象的worker
  /// Keeps track of workers waiting for objects
  absl::flat_hash_map<ObjectID, absl::flat_hash_set<std::shared_ptr<WorkerInterface>>>
      async_plasma_objects_notification_ GUARDED_BY(plasma_object_notification_lock_);

  /// 用于报告度量的字段
  /// 调试状态转储之间的时间间隔
  /// Fields that are used to report metrics.
  /// The period between debug state dumps.
  uint64_t record_metrics_period_ms_;

  /// 上次记录度量的时间
  /// Last time metrics are recorded.
  uint64_t last_metrics_recorded_at_ms_;

  /// 接受并调度的任务数量
  /// Number of tasks that are received and scheduled.
  uint64_t metrics_num_task_scheduled_;

  /// 该节点执行的任务数量
  /// Number of tasks that are executed at this node.
  uint64_t metrics_num_task_executed_;

  /// 传回其他节点的任务数
  /// Number of tasks that are spilled back to other nodes.
  uint64_t metrics_num_task_spilled_back_;

  /// 管理与捆绑包有关的操作
  /// Managers all bundle-related operations.
  std::shared_ptr<PlacementGroupResourceManager> placement_group_resource_manager_;

  /// 管理本地所有运行时环境
  /// Manage all runtime env locally
  RuntimeEnvManager runtime_env_manager_;

  /// 下一个资源广播序列号。非递增的序列号表明网络问题
  /// 如 丢弃，复制，ooo数据包等
  /// Next resource broadcast seq no. Non-incrementing sequence numbers
  /// indicate network issues (dropped/duplicated/ooo packets, etc).
  int64_t next_resource_seq_no_;
};

}  // namespace raylet

}  // end namespace ray
