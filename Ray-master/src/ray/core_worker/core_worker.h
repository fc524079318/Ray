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

#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "ray/common/asio/periodical_runner.h"
#include "ray/common/buffer.h"
#include "ray/common/placement_group.h"
#include "ray/core_worker/actor_handle.h"
#include "ray/core_worker/actor_manager.h"
#include "ray/core_worker/common.h"
#include "ray/core_worker/context.h"
#include "ray/core_worker/future_resolver.h"
#include "ray/core_worker/gcs_server_address_updater.h"
#include "ray/core_worker/lease_policy.h"
#include "ray/core_worker/object_recovery_manager.h"
#include "ray/core_worker/profiling.h"
#include "ray/core_worker/reference_count.h"
#include "ray/core_worker/store_provider/memory_store/memory_store.h"
#include "ray/core_worker/store_provider/plasma_store_provider.h"
#include "ray/core_worker/transport/direct_actor_transport.h"
#include "ray/core_worker/transport/direct_task_transport.h"
#include "ray/gcs/gcs_client.h"
#include "ray/pubsub/publisher.h"
#include "ray/pubsub/subscriber.h"
#include "ray/raylet_client/raylet_client.h"
#include "ray/rpc/node_manager/node_manager_client.h"
#include "ray/rpc/worker/core_worker_client.h"
#include "ray/rpc/worker/core_worker_server.h"
#include "ray/util/process.h"
#include "src/ray/protobuf/pubsub.pb.h"

/// gRPC处理程序的集合及其相关的并发级别
/// 如果你想给worker gRPC服务器添加一个新的调用，请执行以下操作: 
/// 1)在core_worker.proto 中添加rpc到CoreWorkerService。 例如“ExampleCall”  
/// 2)在core_worker_server.h中添加一个新的宏到RAY_CORE_WORKER_DECLARE_RPC_HANDLERS中，
/// 例如: “DECLARE_VOID_RPC_SERVICE_HANDLER_METHOD (ExampleCall)”  
/// 3)在core_worker_server.h中添加一个新的宏到RAY_CORE_WORKER_RPC_HANDLERS中，
/// 例如: “RPC_SERVICE_HANDLER (CoreWorkerService ExampleCall 1)”  
/// 4)在CoreWorker类中添加一个方法:  "CoreWorker::HandleExampleCall"

/// The set of gRPC handlers and their associated level of concurrency. If you want to
/// add a new call to the worker gRPC server, do the following:
/// 1) Add the rpc to the CoreWorkerService in core_worker.proto, e.g., "ExampleCall"
/// 2) Add a new macro to RAY_CORE_WORKER_DECLARE_RPC_HANDLERS
///    in core_worker_server.h,
//     e.g. "DECLARE_VOID_RPC_SERVICE_HANDLER_METHOD(ExampleCall)"
/// 3) Add a new macro to RAY_CORE_WORKER_RPC_HANDLERS in core_worker_server.h, e.g.
///    "RPC_SERVICE_HANDLER(CoreWorkerService, ExampleCall, 1)"
/// 4) Add a method to the CoreWorker class below: "CoreWorker::HandleExampleCall"

namespace ray {
namespace core {

class CoreWorker;
///如果你改变了这个选项的定义，你必须改变  其他文件。 请进行全局搜索并修改它们!!  
// If you change this options's definition, you must change the options used in
// other files. Please take a global search and modify them !!!
struct CoreWorkerOptions {
  // Callback that must be implemented and provided by the language-specific worker
  // frontend to execute tasks and return their results.
  using TaskExecutionCallback = std::function<Status(
      TaskType task_type, const std::string task_name, const RayFunction &ray_function,
      const std::unordered_map<std::string, double> &required_resources,
      const std::vector<std::shared_ptr<RayObject>> &args,
      const std::vector<rpc::ObjectReference> &arg_refs,
      const std::vector<ObjectID> &return_ids, const std::string &debugger_breakpoint,
      std::vector<std::shared_ptr<RayObject>> *results,
      std::shared_ptr<LocalMemoryBuffer> &creation_task_exception_pb_bytes,
      bool *is_application_level_error)>;

  CoreWorkerOptions()
      : store_socket(""),
        raylet_socket(""),
        enable_logging(false),
        log_dir(""),
        install_failure_signal_handler(false),
        interactive(false),
        node_ip_address(""),
        node_manager_port(0),
        raylet_ip_address(""),
        driver_name(""),
        stdout_file(""),
        stderr_file(""),
        task_execution_callback(nullptr),
        check_signals(nullptr),
        gc_collect(nullptr),
        spill_objects(nullptr),
        restore_spilled_objects(nullptr),
        delete_spilled_objects(nullptr),
        unhandled_exception_handler(nullptr),
        get_lang_stack(nullptr),
        kill_main(nullptr),
        is_local_mode(false),
        num_workers(0),
        terminate_asyncio_thread(nullptr),
        serialized_job_config(""),
        metrics_agent_port(-1),
        connect_on_start(true),
        runtime_env_hash(0),
        worker_shim_pid(0) {}
  ///这个worker的类型(例如，DRIVER或worker)。  
  /// Type of this worker (i.e., DRIVER or WORKER).
  WorkerType worker_type;
  /// worker的应用语言（例如，Python或者Java）
  /// Application language of this worker (i.e., PYTHON or JAVA).
  Language language;
  /// 要连接到哪个Object store
  /// Object store socket to connect to.
  std::string store_socket;
  /// 要连接到的raylet ocket
  /// Raylet socket to connect to.
  std::string raylet_socket;
  /// 该worker的job id
  /// Job ID of this worker.
  JobID job_id;
  /// GCS客户端的选项
  /// Options for the GCS client.
  gcs::GcsClientOptions gcs_options;
  /// 若为true，则初始化日志。否则又调用者初始化和清理
  /// Initialize logging if true. Otherwise, it must be initialized and cleaned up by the
  /// caller.
  bool enable_logging;
  /// 写入日志的路径。如果这是空的,日志不会写入到一个文件中。
  /// Directory to write logs to. If this is empty, logs won't be written to a file.
  std::string log_dir;
  /// 若为false，将不会调用'RayLog::InstallFailureSignalHandler()'
  /// If false, will not call `RayLog::InstallFailureSignalHandler()`.
  bool install_failure_signal_handler;
  /// 这个worker是否运行在一个tty上
  /// Whether this worker is running in a tty.
  bool interactive;
  /// 该节点的ip地址
  /// IP address of the node.
  std::string node_ip_address;
  /// 本地raylet的端口号
  /// Port of the local raylet.
  int node_manager_port;
  /// raylet的ip地址
  /// IP address of the raylet.
  std::string raylet_ip_address;
  /// driver的名称
  /// The name of the driver.
  std::string driver_name;
  /// 该进程的标准输出文件
  /// The stdout file of this process.
  std::string stdout_file;
  /// 该进程的标准错误文件
  /// The stderr file of this process.
  std::string stderr_file;
  /// worker回调执行任务的语言
  /// Language worker callback to execute tasks.
  TaskExecutionCallback task_execution_callback;
  /// 关闭一个“CoreWorker”实例时要调用的回调
  /// The callback to be called when shutting down a `CoreWorker` instance.
  std::function<void(const WorkerID &)> on_worker_shutdown;
  /// 应用程序语言回调检查自调用c++以来收到的信号。
  /// 在长时间运行的操作中,这将周期性地调用(至少每秒一次)
  /// 如果函数返回任何状态,但是状态OK,那么核心工作人员的任何长时间运行将会短路并返回该状态。 
  /// Application-language callback to check for signals that have been received
  /// since calling into C++. This will be called periodically (at least every
  /// 1s) during long-running operations. If the function returns anything but StatusOK,
  /// any long-running operations in the core worker will short circuit and return that
  /// status.
  std::function<Status()> check_signals;
  /// 应用程序语言回调,以触发语言运行中的垃圾收集机制。
  /// 这需要释放分布式引用，否则可能会在垃圾对象中被保存。 
  /// Application-language callback to trigger garbage collection in the language
  /// runtime. This is required to free distributed references that may otherwise
  /// be held up in garbage objects.
  std::function<void()> gc_collect;
  /// 应用程序语言回调，从而将对象释放到外部存储。
  /// Application-language callback to spill objects to external storage.
  std::function<std::vector<std::string>(const std::vector<rpc::ObjectReference> &)>
      spill_objects;
  /// 应用程序语言回调,从而恢复外部存储的对象。
  /// Application-language callback to restore objects from external storage.
  std::function<int64_t(const std::vector<rpc::ObjectReference> &,
                        const std::vector<std::string> &)>
      restore_spilled_objects;
  /// 应用程序语言回调从而删除外部存储的对象
  /// Application-language callback to delete objects from external storage.
  std::function<void(const std::vector<std::string> &, rpc::WorkerType)>
      delete_spilled_objects;
  /// 用来调用从未检索到的错误对象的函数
  /// Function to call on error objects never retrieved.
  std::function<void(const RayObject &error)> unhandled_exception_handler;
  std::function<void(const std::string &, const std::vector<std::string> &)>
      run_on_util_worker_handler;
  /// 对应语言worker回调从而获得当前调用堆栈
  /// Language worker callback to get the current call stack.
  std::function<void(std::string *)> get_lang_stack;
  /// 试图中断当前运行Python线程的函数
  // Function that tries to interrupt the currently running Python thread.
  std::function<bool()> kill_main;
  /// 本地模式是否启用
  /// Is local mode being used.
  bool is_local_mode;
  /// 当前进程上启动的worker的数量
  /// The number of workers to be started in the current process.
  int num_workers;
  /// 破坏异步事件和循环的函数
  /// The function to destroy asyncio event and loops.
  std::function<void()> terminate_asyncio_thread;
  /// 业务配置的序列化表示
  /// Serialized representation of JobConfig.
  std::string serialized_job_config;
  /// 从core worker导入指标的度量代理的端口号
  /// 若为-1，则说明没有这样的代理
  /// The port number of a metrics agent that imports metrics from core workers.
  /// -1 means there's no such agent.
  int metrics_agent_port;
  /// 若为false，构造函数不会连接并通知raylets它已经准备好了。
  /// 应该通过使用CoreWorker::Start.TODO(sang):的调用者显式地启动它 
  /// 使用这种方法对Java和cpp前端进行使用。
  /// If false, the constructor won't connect and notify raylets that it is
  /// ready. It should be explicitly startd by a caller using CoreWorker::Start.
  /// TODO(sang): Use this method for Java and cpp frontend too.
  bool connect_on_start;
  /// 这个worker运行时环境的哈希
  /// The hash of the runtime env for this worker.
  int runtime_env_hash;
  /// 用于设置worker运行环境的进程的PID
  /// The PID of the process for setup worker runtime env.
  pid_t worker_shim_pid;
};
/// 一个进程中一个或多个“CoreWorker”实例的生命周期管理。
/// Lifecycle management of one or more `CoreWorker` instances in a process.
///
/// To start a driver in the current process:
///     CoreWorkerOptions options = {
///         WorkerType::DRIVER,             // worker_type
///         ...,                            // other arguments
///         1,                              // num_workers
///     };
///     CoreWorkerProcess::Initialize(options);
///
/// To shutdown a driver in the current process:
///     CoreWorkerProcess::Shutdown();
///
/// To start one or more workers in the current process:
///     CoreWorkerOptions options = {
///         WorkerType::WORKER,             // worker_type
///         ...,                            // other arguments
///         num_workers,                    // num_workers
///     };
///     CoreWorkerProcess::Initialize(options);
///     ...                                 // Do other stuff
///     CoreWorkerProcess::RunTaskExecutionLoop();
///
/// To shutdown a worker in the current process, return a system exit status (with status
/// code `IntentionalSystemExit` or `UnexpectedSystemExit`) in the task execution
/// callback.
///
/// If more than 1 worker is started, only the threads which invoke the
/// `task_execution_callback` will be automatically associated with the corresponding
/// worker. If you started your own threads and you want to use core worker APIs in these
/// threads, remember to call `CoreWorkerProcess::SetCurrentThreadWorkerId(worker_id)`
/// once in the new thread before calling core worker APIs, to associate the current
/// thread with a worker. You can obtain the worker ID via
/// `CoreWorkerProcess::GetCoreWorker()->GetWorkerID()`. Currently a Java worker process
/// starts multiple workers by default, but can be configured to start only 1 worker by
/// speicifying `num_java_workers_per_process` in the job config.
///
/// If only 1 worker is started (either because the worker type is driver, or the
/// `num_workers` in `CoreWorkerOptions` is set to 1), all threads will be automatically
/// associated to the only worker. Then no need to call `SetCurrentThreadWorkerId` in
/// your own threads. Currently a Python worker process starts only 1 worker.
class CoreWorkerProcess {
 public:
  ///
  /// Driver和Worker模式下的公共方法
  /// Public methods used in both DRIVER and WORKER mode.
  ///

  /// 
  /// 在流程级别初始化core worker
  /// Initialize core workers at the process level.
  ///
  /// 参数[in]options 各种初始化选项
  /// \param[in] options The various initialization options.
  static void Initialize(const CoreWorkerOptions &options);

  /// 获取与当前线程相关的core worker
  /// 注意(kfstorm):在这里,我们返回一个引用,而不是“shared_ptr”,
  /// 以确保CoreWorkerProcess完全控制“CoreWorker”的销毁时间
  /// Get the core worker associated with the current thread.
  /// NOTE (kfstorm): Here we return a reference instead of a `shared_ptr` to make sure
  /// `CoreWorkerProcess` has full control of the destruction timing of `CoreWorker`.
  static CoreWorker &GetCoreWorker();

  /// 尝试通过worker Id获得'CoreWorker'接口
  /// 如果当前线程没有关联的core worker，返回一个空指针
  /// Try to get the `CoreWorker` instance by worker ID.
  /// If the current thread is not associated with a core worker, returns a null pointer.
  ///
  /// \参数[in] workerId 该worker的id
  /// \返回 'CoreWorker'的接口
  /// \param[in] workerId The worker ID.
  /// \return The `CoreWorker` instance.
  static std::shared_ptr<CoreWorker> TryGetWorker(const WorkerID &worker_id);

  /// 通过worker ID设置core worke与当前线程相关联
  /// 目前仅由Java worker使用
  /// \参数 worker_id core worker接口的ID
  /// Set the core worker associated with the current thread by worker ID.
  /// Currently used by Java worker only.
  ///
  /// \param worker_id The worker ID of the core worker instance.
  static void SetCurrentThreadWorkerId(const WorkerID &worker_id);

  /// 当前进程是否已初始化为core worker
  /// Whether the current process has been initialized for core worker.
  static bool IsInitialized();

  /// 
  /// 仅在Driver模式使用的公共方法
  /// Public methods used in DRIVER mode only.
  ///

  /// 在进程层级完全结束driver
  /// Shutdown the driver completely at the process level.
  static void Shutdown();

  /// 
  /// 仅在Worker模式下使用的公共方法
  /// Public methods used in WORKER mode only.
  ///

  /// 开始接受和执行任务
  /// Start receiving and executing tasks.
  static void RunTaskExecutionLoop();

  // 破坏器不被用作公共API,但它是由智能指针所需要的
  // The destructor is not to be used as a public API, but it's required by smart
  // pointers.
  ~CoreWorkerProcess();

 private:
  /// 使用适当的选项创建一个“CoreWorkerProcess”。 
  /// \参数[in] options 各种初始化选项
  /// Create an `CoreWorkerProcess` with proper options.
  ///
  /// \param[in] options The various initialization options.
  CoreWorkerProcess(const CoreWorkerOptions &options);

  /// 检查这个进程中的core worker的环境是否被初始化。 
  /// \返回 空
  /// Check that the core worker environment is initialized for this process.
  ///
  /// \return Void.
  static void EnsureInitialized();

  static void HandleAtExit();

  void InitializeSystemConfig();

  ///通过worker ID获得'CoreWorker'接口
  /// \参数[in] workerId worker的Id
  /// \返回 该'coreworker'的接口
  /// Get the `CoreWorker` instance by worker ID.
  ///
  /// \param[in] workerId The worker ID.
  /// \return The `CoreWorker` instance.
  std::shared_ptr<CoreWorker> GetWorker(const WorkerID &worker_id) const
      LOCKS_EXCLUDED(worker_map_mutex_);

  /// 创建一个新的'Coreworker'接口
  /// \参数 新创建的'Coreworker' 接口
  /// Create a new `CoreWorker` instance.
  ///
  /// \return The newly created `CoreWorker` instance.
  std::shared_ptr<CoreWorker> CreateWorker() LOCKS_EXCLUDED(worker_map_mutex_);

  /// 移除当前存在的coreworker接口
  /// \参数[in] 当前存在的CoreWorker的接口
  /// \返回 空
  /// Remove an existing `CoreWorker` instance.
  ///
  /// \param[in] The existing `CoreWorker` instance.
  /// \return Void.
  void RemoveWorker(std::shared_ptr<CoreWorker> worker) LOCKS_EXCLUDED(worker_map_mutex_);

  /// 各种选项
  /// The various options.
  const CoreWorkerOptions options_;

  /// 与当前线程关联的core worker接口
  /// 在这里使用weak_ptr来避免由于多线程的内存泄漏。 
  /// The core worker instance associated with the current thread.
  /// Use weak_ptr here to avoid memory leak due to multi-threading.
  static thread_local std::weak_ptr<CoreWorker> current_core_worker_;

  /// 唯一的core worker 接口，如果workers的数目为1
  /// The only core worker instance, if the number of workers is 1.
  std::shared_ptr<CoreWorker> global_worker_;

  ///全局worker的worker id，如果workers的数目为1
  /// The worker ID of the global worker, if the number of workers is 1.
  const WorkerID global_worker_id_;

  /// 从worker ID到worker的图
  /// Map from worker ID to worker.
  std::unordered_map<WorkerID, std::shared_ptr<CoreWorker>> workers_
      GUARDED_BY(worker_map_mutex_);

  /// 保护访问'workers_'图
  /// To protect accessing the `workers_` map.
  mutable absl::Mutex worker_map_mutex_;
};

/// 包含所有核心和语言独立功能的根类。
/// 这个类应该用于实现应用程序语言(Java、Python等)的workers。
/// The root class that contains all the core and language-independent functionalities
/// of the worker. This class is supposed to be used to implement app-language (Java,
/// Python, etc) workers.
class CoreWorker : public rpc::CoreWorkerServiceHandler {
 public:
  /// 构建一个CoreWorker实例
  ///
  /// \参数[in] options 各种初始化参数
  /// \参数[in] worker_id 该worker的ID
  /// Construct a CoreWorker instance.
  ///
  /// \param[in] options The various initialization options.
  /// \param[in] worker_id ID of this worker.
  CoreWorker(const CoreWorkerOptions &options, const WorkerID &worker_id);

  CoreWorker(CoreWorker const &) = delete;

  void operator=(CoreWorker const &other) = delete;

  ///
  /// “CoreWorkerProcess”和“CoreWorker”本身使用的公共方法。
  /// Public methods used by `CoreWorkerProcess` and `CoreWorker` itself.
  ///

  /// 连接到raylet并通知core worker准备好了
  /// 如果options.connect_on_start是false，他就不需要被显性调用
  /// Connect to the raylet and notify that the core worker is ready.
  /// If the options.connect_on_start is false, it doesn't need to be explicitly
  /// called.
  void ConnectToRaylet();

  /// 温和地将这个worker从Raylet上断开连接
  /// 如果这个函数在shutdown期间被调用，那么Raylet将会认为这是一次主动的断开连接
  /// \返回 空
  /// Gracefully disconnect the worker from Raylet.
  /// If this function is called during shutdown, Raylet will treat it as an intentional
  /// disconnect.
  ///
  /// \return Void.
  void Disconnect(rpc::WorkerExitType exit_type = rpc::WorkerExitType::INTENDED_EXIT,
                  const std::shared_ptr<LocalMemoryBuffer>
                      &creation_task_exception_pb_bytes = nullptr);

  /// 完全结束这个worker
  /// \返回 空
  /// Shut down the worker completely.
  ///
  /// \return void.
  void Shutdown();

  /// 阻塞当前线程直到这个worker关闭
  /// Block the current thread until the worker is shut down.
  void WaitForShutdown();

  /// 开始接收和执行任务
  /// \返回 空
  /// Start receiving and executing tasks.
  /// \return void.
  void RunTaskExecutionLoop();

  const WorkerID &GetWorkerID() const;

  WorkerType GetWorkerType() const { return options_.worker_type; }

  Language GetLanguage() const { return options_.language; }

  WorkerContext &GetWorkerContext() { return worker_context_; }

  const TaskID &GetCurrentTaskId() const { return worker_context_.GetCurrentTaskID(); }

  const JobID &GetCurrentJobId() const { return worker_context_.GetCurrentJobID(); }

  NodeID GetCurrentNodeId() const { return NodeID::FromBinary(rpc_address_.raylet_id()); }

  const PlacementGroupID &GetCurrentPlacementGroupId() const {
    return worker_context_.GetCurrentPlacementGroupId();
  }

  bool ShouldCaptureChildTasksInPlacementGroup() const {
    return worker_context_.ShouldCaptureChildTasksInPlacementGroup();
  }

  bool GetCurrentTaskRetryExceptions() const {
    if (!options_.is_local_mode) {
      return worker_context_.GetCurrentTask()->GetMessage().retry_exceptions();
    } else {
      return false;
    }
  }

  void SetWebuiDisplay(const std::string &key, const std::string &message);

  void SetActorTitle(const std::string &title);

  void SetCallerCreationTimestamp();

  /// 增加这个对象Id的引用计数
  /// 增加该对象ID的本地引用计数。当创建新的引用时,应该调用语言前端。 
  /// \参数[in] object_id 增加引用计数的对象ID。 
  /// Increase the reference count for this object ID.
  /// Increase the local reference count for this object ID. Should be called
  /// by the language frontend when a new reference is created.
  ///
  /// \param[in] object_id The object ID to increase the reference count for.
  void AddLocalReference(const ObjectID &object_id) {
    AddLocalReference(object_id, CurrentCallSite());
  }

  /// 减少该对象ID的引用计数。当引用被销毁时,应该调用语言。 
  /// \参数[in] object_id 减少引用计数的对象id
  /// Decrease the reference count for this object ID. Should be called
  /// by the language frontend when a reference is destroyed.
  ///
  /// \param[in] object_id The object ID to decrease the reference count for.
  void RemoveLocalReference(const ObjectID &object_id) {
    std::vector<ObjectID> deleted;
    reference_counter_->RemoveLocalReference(object_id, &deleted);
    // TOOD(ilr): better way of keeping an object from being deleted
    if (!options_.is_local_mode) {
      memory_store_->Delete(deleted);
    }
  }
  /// 以map形式返回当前范围内的所有objectid，以及它们的(local, submitted_task)引用计数对。
  /// 用于调试目的
  /// Returns a map of all ObjectIDs currently in scope with a pair of their
  /// (local, submitted_task) reference counts. For debugging purposes.
  std::unordered_map<ObjectID, std::pair<size_t, size_t>> GetAllReferenceCounts() const;

  /// 将对象放入plasma中，这是Put的一个版本，直接把对象放入plasma中，然后固定物体。
  /// \参数[in] ray的object
  /// \参数[in] 要序列化的对象id
  /// 附加到序列化的对象id
  /// Put an object into plasma. It's a version of Put that directly put the
  /// object into plasma and also pin the object.
  ///
  /// \param[in] The ray object.
  /// \param[in] object_id The object ID to serialize.
  /// appended to the serialized object ID.
  void PutObjectIntoPlasma(const RayObject &object, const ObjectID &object_id);

  /// 将一个对象推到plasma，如果这个对象已经在本地存在，它将被放入plasma store
  /// 如果它还不存在，一旦可用，就会溢出到plasma中。
  /// \参数[in] object_id 要序列化的对象id
  /// 附加到序列化的对象id
  /// Promote an object to plasma. If the
  /// object already exists locally, it will be put into the plasma store. If
  /// it doesn't yet exist, it will be spilled to plasma once available.
  ///
  /// \param[in] object_id The object ID to serialize.
  /// appended to the serialized object ID.
  void PromoteObjectToPlasma(const ObjectID &object_id);

  /// 获得该worker的RPC地址
  /// \参数[out] 该worker的RPC地址
  /// Get the RPC address of this worker.
  ///
  /// \param[out] The RPC address of this worker.
  const rpc::Address &GetRpcAddress() const;

  /// 获得拥有给定对象的worker的RPC地址
  /// \参数[in] object_id   对象的id，该对象要么是我们拥有的，
  /// 要么是调用者已经添加了所有权信息(通过RegisterOwnershipInfoAndResolveFuture)。
  /// \参数[out]拥有这个对象的worker的RPC地址
  /// Get the RPC address of the worker that owns the given object.
  ///
  /// \param[in] object_id The object ID. The object must either be owned by
  /// us, or the caller previously added the ownership information (via
  /// RegisterOwnershipInfoAndResolveFuture).
  /// \param[out] The RPC address of the worker that owns this object.
  rpc::Address GetOwnerAddress(const ObjectID &object_id) const;
  /// 这段注释和上边完全一样，感觉像是写重了
  /// 获取拥有给定对象的worker的RPC地址
  /// \参数[in] object_id 
  /// \参数[out] 地址
  /// Get the RPC address of the worker that owns the given object.
  ///
  /// \param[in] object_id The object ID. The object must either be owned by
  /// us, or the caller previously added the ownership information (via
  /// RegisterOwnershipInfoAndResolveFuture).
  /// \param[out] The RPC address of the worker that owns this object.
  std::vector<rpc::ObjectReference> GetObjectRefs(
      const std::vector<ObjectID> &object_ids) const;

  /// 获取对象的所有者信息。 
  /// 当序列化一个对象ID时应该调用这个函数，并且返回的信息应该与序列化的对象ID一起存储。  
  /// 
  /// 这只能在我们通过任务提交创建的对象id上调用，ray.put,或者反序列化的对象id。
  /// 它不能在随机创建的对象id上调用，例如objecd:: fromrrandom。  
  /// 后置条件 Get(object_id)可用
  /// \参数[in] object_id
  /// \参数[out] owner_address 对象所有者的地址，这个应该被绑定到这个id上
  /// \参数[out] serialized_object_status 序列化对象状态protobuf
  /// Get the owner information of an object. This should be
  /// called when serializing an object ID, and the returned information should
  /// be stored with the serialized object ID.
  ///
  /// This can only be called on object IDs that we created via task
  /// submission, ray.put, or object IDs that we deserialized. It cannot be
  /// called on object IDs that were created randomly, e.g.,
  /// ObjectID::FromRandom.
  ///
  /// Postcondition: Get(object_id) is valid.
  ///
  /// \param[in] object_id The object ID to serialize.
  /// appended to the serialized object ID.
  /// \param[out] owner_address The address of the object's owner. This should
  /// be appended to the serialized object ID.
  /// \param[out] serialized_object_status The serialized object status protobuf.
  void GetOwnershipInfo(const ObjectID &object_id, rpc::Address *owner_address,
                        std::string *serialized_object_status);

  /// 添加一个对被语言前端反序列化的ObjectID的引用。
  /// 这也将启动解决未来问题的进程。 
  /// 具体来说，我们会定期联系所有者，直到我们知道对象已经创建或所有者不再可达。
  ///  这将解除依赖于该对象的任何get或任务提交的阻塞。  
  /// \参数[in] object_id 
  /// \参数[in] out_object_id 包含object_id的对象ID(如果有的话)。 
  /// 如果对象ID直接内联在任务规范中，或者由应用程序传递到带外(由字节字符串反序列化)，则该值可能为nil。
  /// \参数[in] owner_address 对象所有者的地址
  /// \参数[in] serialized_object_status 序列化对象状态protobuf
  /// Add a reference to an ObjectID that was deserialized by the language
  /// frontend. This will also start the process to resolve the future.
  /// Specifically, we will periodically contact the owner, until we learn that
  /// the object has been created or the owner is no longer reachable. This
  /// will then unblock any Gets or submissions of tasks dependent on the
  /// object.
  ///
  /// \param[in] object_id The object ID to deserialize.
  /// \param[in] outer_object_id The object ID that contained object_id, if
  /// any. This may be nil if the object ID was inlined directly in a task spec
  /// or if it was passed out-of-band by the application (deserialized from a
  /// byte string).
  /// \param[in] owner_address The address of the object's owner.
  /// \param[in] serialized_object_status The serialized object status protobuf.
  void RegisterOwnershipInfoAndResolveFuture(const ObjectID &object_id,
                                             const ObjectID &outer_object_id,
                                             const rpc::Address &owner_address,
                                             const std::string &serialized_object_status);

  ///
  /// 与存储和检索对象相关的公共方法。
  /// Public methods related to storing and retrieving objects.
  ///

  /// 将一个对象放入object store
  /// 参数[in] object ray的object
  /// 参数[in] contained_object_ids   在该对象中序列化的id
  /// 参数[out] object_id 生成的对象id
  /// 返回 状态
  /// Put an object into object store.
  ///
  /// \param[in] object The ray object.
  /// \param[in] contained_object_ids The IDs serialized in this object.
  /// \param[out] object_id Generated ID of the object.
  /// \return Status.
  Status Put(const RayObject &object, const std::vector<ObjectID> &contained_object_ids,
             ObjectID *object_id);

  /// 将一个对象以规定的id放进object store
  /// 参数[in] object 
  /// 参数[in] contained_object_ids 这个对象中序列化的id
  /// 参数[in] object_id 由用户给定的object_id
  /// 参数[in] pin_object 是否告诉raylet固定这个对象
  /// 返回 状态
  /// Put an object with specified ID into object store.
  ///
  /// \param[in] object The ray object.
  /// \param[in] contained_object_ids The IDs serialized in this object.
  /// \param[in] object_id Object ID specified by the user.
  /// \param[in] pin_object Whether or not to tell the raylet to pin this object.
  /// \return Status.
  Status Put(const RayObject &object, const std::vector<ObjectID> &contained_object_ids,
             const ObjectID &object_id, bool pin_object = false);
  /// 在对象存储中创建并返回一个可以直接写入的缓冲区。
  /// 写入到缓冲区后，调用者必须调用'SealOwned()'来完成对象。 
  ///'CreateOwned()'和'SealOwned()'的组合是'Put()'的替代接口，允许前端在可能的情况下避免额外的副本。 
  /// 参数[in] metadata 待写入对象的元数据
  /// 参数[in] data_size 被写入的对象的大小
  /// 参数[in] contained_object_ids 这个对象中序列化的id
  /// 参数[out] object_id put操作生成的object id
  /// 参数[out] data 用户写入对象的缓冲区
  /// 参数[in] created_by_worker 是否由worker创建
  /// 参数[in] owner_address object的所有者的地址。若没有提供，默认为当前worker
  /// 参数[in] inline_small_object 是否内联创建该对象，如果他是小的。
  /// Create and return a buffer in the object store that can be directly written
  /// into. After writing to the buffer, the caller must call `SealOwned()` to
  /// finalize the object. The `CreateOwned()` and `SealOwned()` combination is
  /// an alternative interface to `Put()` that allows frontends to avoid an extra
  /// copy when possible.
  ///
  /// \param[in] metadata Metadata of the object to be written.
  /// \param[in] data_size Size of the object to be written.
  /// \param[in] contained_object_ids The IDs serialized in this object.
  /// \param[out] object_id Object ID generated for the put.
  /// \param[out] data Buffer for the user to write the object into.
  /// \param[in] object create by worker or not.
  /// \param[in] owner_address The address of object's owner. If not provided,
  /// defaults to this worker.
  /// \param[in] inline_small_object wether to inline create this object if it's
  /// small.
  /// \return Status.
  Status CreateOwned(const std::shared_ptr<Buffer> &metadata, const size_t data_size,
                     const std::vector<ObjectID> &contained_object_ids,
                     ObjectID *object_id, std::shared_ptr<Buffer> *data,
                     bool created_by_worker,
                     const std::unique_ptr<rpc::Address> &owner_address = nullptr,
                     bool inline_small_object = true);
  /// 为已经存在的对象ID创建并返回一个可以直接写入的object store缓冲区	
  /// 写入到缓冲区后，调用者必须调用' SealExisting() '来完成对象。
  /// 'CreateExisting()'和'SealExisting()'的组合是'Put()'的另一种接口，允许前端在可能的情况下避免额外的副本。
  /// 参数[in] metadata 待写入对象的元数据
  /// 参数[in] data_size  被写入的对象的大小
  /// 参数[in] object_id 用户指定的对象id
  /// 参数[in] owner_address 对象所有者的地址
  /// 参数[out] data 让用户写入对象的缓冲区
  /// 返回 Status
  /// Create and return a buffer in the object store that can be directly written
  /// into, for an object ID that already exists. After writing to the buffer, the
  /// caller must call `SealExisting()` to finalize the object. The `CreateExisting()`
  /// and `SealExisting()` combination is an alternative interface to `Put()` that
  /// allows frontends to avoid an extra copy when possible.
  ///
  /// \param[in] metadata Metadata of the object to be written.
  /// \param[in] data_size Size of the object to be written.
  /// \param[in] object_id Object ID specified by the user.
  /// \param[in] owner_address The address of the object's owner.
  /// \param[out] data Buffer for the user to write the object into.
  /// \return Status.
  Status CreateExisting(const std::shared_ptr<Buffer> &metadata, const size_t data_size,
                        const ObjectID &object_id, const rpc::Address &owner_address,
                        std::shared_ptr<Buffer> *data, bool created_by_worker);

  /// 确定将对象放入对象存储区。 
  /// 应该在相应的'CreateOwned()'调用之后调用，然后写入返回的缓冲区。  
  /// 参数[in] object_id 对象所对应的对象id
  /// 参数[in] pin_object 是否将该对象pin到raylet上
  /// 参数[in] owner_address 对象所有者的地址，若未提供，默认为当前worker
  /// 返回 状态
  /// Finalize placing an object into the object store. This should be called after
  /// a corresponding `CreateOwned()` call and then writing into the returned buffer.
  ///
  /// \param[in] object_id Object ID corresponding to the object.
  /// \param[in] pin_object Whether or not to pin the object at the local raylet.
  /// \param[in] The address of object's owner. If not provided,
  /// defaults to this worker.
  /// \return Status.
  Status SealOwned(const ObjectID &object_id, bool pin_object,
                   const std::unique_ptr<rpc::Address> &owner_address = nullptr);

  /// 确定将对象放入对象存储区。
  /// 这应该在相应的' CreateExisting() '调用之后调用，然后写入返回的缓冲区。
  /// 参数[in] object_id 对象对应的对象ID。
  /// 参数[in] pin_object 是否将该对象pin到raylet上
  /// 参数[in] owner_address 对象所有者的地址，如果对象是固定的，那么raylet将联系到该对象。 
  /// 如果没有提供，则默认为该worker。  
  /// 返回 状态
  /// Finalize placing an object into the object store. This should be called after
  /// a corresponding `CreateExisting()` call and then writing into the returned buffer.
  ///
  /// \param[in] object_id Object ID corresponding to the object.
  /// \param[in] pin_object Whether or not to pin the object at the local raylet.
  /// \param[in] owner_address Address of the owner of the object who will be contacted by
  /// the raylet if the object is pinned. If not provided, defaults to this worker.
  /// \return Status.
  Status SealExisting(const ObjectID &object_id, bool pin_object,
                      const std::unique_ptr<rpc::Address> &owner_address = nullptr);

  /// 从对象存储中获取对象列表。 检索失败的对象将作为nullptrs返回。  
  /// 参数[in] ids 要获取的对象们的id们
  /// 参数[in] timeout_ms   超时以毫秒为单位，如果为负，则等待无限次。 
  /// 参数[out] results 对象数据表
  /// 返回 Status
  /// Get a list of objects from the object store. Objects that failed to be retrieved
  /// will be returned as nullptrs.
  ///
  /// \param[in] ids IDs of the objects to get.
  /// \param[in] timeout_ms Timeout in milliseconds, wait infinitely if it's negative.
  /// \param[out] results Result list of objects data.
  /// \return Status.
  Status Get(const std::vector<ObjectID> &ids, const int64_t timeout_ms,
             std::vector<std::shared_ptr<RayObject>> *results);

  /// 直接从本地等plasma store获取对象，而不需要等待从另一个节点获取对象。
  /// 这只能在内部使用，不能由用户代码使用。
  /// 注意:该方法的调用者应该保证该对象已经存在于plasma store中，因此它不需要从其他节点获取
  ///
  /// 参数[in] ids 要获取的对象们的id
  /// 参数[out] results 结果将储存在这里。 将为不在本地存储中的对象添加nullptr。
  /// 返回 
  /// Get objects directly from the local plasma store, without waiting for the
  /// objects to be fetched from another node. This should only be used
  /// internally, never by user code.
  /// NOTE: Caller of this method should guarantee that the object already exists in the
  /// plasma store, thus it doesn't need to fetch from other nodes.
  ///
  /// \param[in] ids The IDs of the objects to get.
  /// \param[out] results The results will be stored here. A nullptr will be
  /// added for objects that were not in the local store.
  /// \return Status OK if all objects were found. Returns ObjectNotFound error
  /// if at least one object was not in the local store.
  Status GetIfLocal(const std::vector<ObjectID> &ids,
                    std::vector<std::shared_ptr<RayObject>> *results);
  /// 返回查询的object store是包含了给定的object
  /// 
  /// 参数[in] object_id 要检查的对象的id
  /// 参数[out] has_object 这个要检查的对象是否存在
  /// 参数[out] is_in_plasma 这个对象是否在plasma
  /// 返回 状态
  /// Return whether or not the object store contains the given object.
  ///
  /// \param[in] object_id ID of the objects to check for.
  /// \param[out] has_object Whether or not the object is present.
  /// \param[out] is_in_plasma Whether or not the object is in Plasma.
  /// \return Status.
  Status Contains(const ObjectID &object_id, bool *has_object,
                  bool *is_in_plasma = nullptr);

  ///等待对象存储中出现对象列表。
  /// 支持重复的对象id，在本例中' num_objects '包含重复的id。
  /// TODO(zhijunfu):在语义上，当有副本时失败，并要求在应用层处理它，这可能更清楚。  
  ///
  /// 参数[in] object_ids 要等待的对象id
  /// 参数[in] num_objects 应该出现的对象的数量
  /// 参数[in] timeout_ms 超时以毫秒为单位，如果为负，则等待无限次。
  /// 参数[out] results 表示每个对象是否出现的bitset。  
  /// 返回 状态
  /// Wait for a list of objects to appear in the object store.
  /// Duplicate object ids are supported, and `num_objects` includes duplicate ids in this
  /// case.
  /// TODO(zhijunfu): it is probably more clear in semantics to just fail when there
  /// are duplicates, and require it to be handled at application level.
  ///
  /// \param[in] IDs of the objects to wait for.
  /// \param[in] num_objects Number of objects that should appear.
  /// \param[in] timeout_ms Timeout in milliseconds, wait infinitely if it's negative.
  /// \param[out] results A bitset that indicates each object has appeared or not.
  /// \return Status.
  Status Wait(const std::vector<ObjectID> &object_ids, const int num_objects,
              const int64_t timeout_ms, std::vector<bool> *results, bool fetch_local);

  /// 从plasma对象存储处删除一系列对象
  /// 参数[in] object_ids 要删除的对象的id
  /// 参数[in] local_only 是否只删除本地节点的对象，或者删除集群上所有节点中的对象
  /// 返回 状态
  /// Delete a list of objects from the plasma object store.
  ///
  /// \param[in] object_ids IDs of the objects to delete.
  /// \param[in] local_only Whether only delete the objects in local node, or all nodes in
  /// the cluster.
  /// \return Status.
  Status Delete(const std::vector<ObjectID> &object_ids, bool local_only);

  /// 获取列表对象的位置。  
  /// 检索失败的位置将作为nullptrs返回。
  ///
  /// 参数[in] object_ids 要获取的对象的id
  /// 参数[in] timeout_ms 超时以毫秒为单位，如果为负，则等待无限次。
  /// 参数[out] results 对象位置的结果列表。
  /// 返回 状态
  /// Get the locations of a list objects. Locations that failed to be retrieved
  /// will be returned as nullptrs.
  ///
  /// \param[in] object_ids IDs of the objects to get.
  /// \param[in] timeout_ms Timeout in milliseconds, wait infinitely if it's negative.
  /// \param[out] results Result list of object locations.
  /// \return Status.
  Status GetLocationFromOwner(const std::vector<ObjectID> &object_ids, int64_t timeout_ms,
                              std::vector<std::shared_ptr<ObjectLocation>> *results);

  /// 触发集群上每个worker的垃圾收集机制
  /// Trigger garbage collection on each worker in the cluster.
  void TriggerGlobalGC();

  /// 获取用于调试目的的描述对象存储内存使用情况的字符串。  
  /// 返回 std::string 描述内存使用情况的字符串
  /// Get a string describing object store memory usage for debugging purposes.
  ///
  /// \return std::string The string describing memory usage.
  std::string MemoryUsageString();

  ///
  /// 与任务提交相关的公共方法
  /// Public methods related to task submission.
  ///

  /// 获取用于从此worker向actor提交任务的调用方ID。
  /// 
  /// 返回 调用方id
  /// 对于非参与者任务，这是当前任务ID。 
  /// 对于actor，这是当前的actor ID。
  /// 为了确保所有调用方ID都具有相同的类型,我们将actor ID与其他ID一起嵌入到TaskID中,并将其余的字节置零。  
  /// Get the caller ID used to submit tasks from this worker to an actor.
  ///
  /// \return The caller ID. For non-actor tasks, this is the current task ID.
  /// For actors, this is the current actor ID. To make sure that all caller
  /// IDs have the same type, we embed the actor ID in a TaskID with the rest
  /// of the bytes zeroed out.
  TaskID GetCallerId() const LOCKS_EXCLUDED(mutex_);

  /// 向相关driver推送一个错误
  ///
  /// 参数[in] job_id 错误所对应的job的id
  /// 参数[in] 错误的类型
  /// 参数[in] 错误的信息
  /// 参数[in]   错误的时间戳
  /// 返回 状态
  
  /// Push an error to the relevant driver.
  ///
  /// \param[in] The ID of the job_id that the error is for.
  /// \param[in] The type of the error.
  /// \param[in] The error message.
  /// \param[in] The timestamp of the error.
  /// \return Status.
  Status PushError(const JobID &job_id, const std::string &type,
                   const std::string &error_message, double timestamp);

  /// 设置具有指定容量和客户端id的资源  
  /// 参数[in] resource_name 被设置的资源的名称
  /// 参数[in] capacity 资源的容量
  /// 参数[in] node_id 资源被设定在哪个节点的id
  /// 返回 状态
  /// Sets a resource with the specified capacity and client id
  /// \param[in] resource_name Name of the resource to be set.
  /// \param[in] capacity Capacity of the resource.
  /// \param[in] node_id NodeID where the resource is to be set.
  /// \return Status
  Status SetResource(const std::string &resource_name, const double capacity,
                     const NodeID &node_id);

  /// 提交一个普通的任务
  /// 参数[in] function 要执行的远程函数
  /// 参数[in] args 此任务的参数
  /// 参数[in] task_optons 此任务的选项
  /// 参数[in] max_retires 当任务失败时的最大重试次数
  /// 参数[in] placement_options 放置组选项
  /// 参数[in] placement_group_capture_child_tasks 是否提交任务
  /// 参数[in] debugger_breakpoint 在此任务开始执行后，调试器要拖放到的断点，如果不想拖放到调试器中，则为""。 
  /// 应该捕获父母的安置组隐式。  
  /// 返回 此任务返回的ObjectRefs
  /// Submit a normal task.
  ///
  /// \param[in] function The remote function to execute.
  /// \param[in] args Arguments of this task.
  /// \param[in] task_options Options for this task.
  /// \param[in] max_retires max number of retry when the task fails.
  /// \param[in] placement_options placement group options.
  /// \param[in] placement_group_capture_child_tasks whether or not the submitted task
  /// \param[in] debugger_breakpoint breakpoint to drop into for the debugger after this
  /// task starts executing, or "" if we do not want to drop into the debugger.
  /// should capture parent's placement group implicilty.
  /// \return ObjectRefs returned by this task.
  std::vector<rpc::ObjectReference> SubmitTask(
      const RayFunction &function, const std::vector<std::unique_ptr<TaskArg>> &args,
      const TaskOptions &task_options, int max_retries, bool retry_exceptions,
      BundleID placement_options, bool placement_group_capture_child_tasks,
      const std::string &debugger_breakpoint);

  /// 创建一个actor
  /// 参数[in] caller_id 任务提交者的id
  /// 参数[in] function 生成这个actor对象的远程函数
  /// 参数[in] args 这个任务的参数
  /// 参数[in] actor_creation_options 这个actor创建任务的选项
  /// 参数[in] extension_data actor句柄的扩展数据，请参见'core_worker.proto'中的'ActorHandle'。
  /// 参数[out] actor_id 被创建的actor的id，他可以用于提交actor上的任务
  /// Create an actor.
  ///
  /// \param[in] caller_id ID of the task submitter.
  /// \param[in] function The remote function that generates the actor object.
  /// \param[in] args Arguments of this task.
  /// \param[in] actor_creation_options Options for this actor creation task.
  /// \param[in] extension_data Extension data of the actor handle,
  /// see `ActorHandle` in `core_worker.proto`.
  /// \param[out] actor_id ID of the created actor. This can be used to submit
  /// tasks on the actor.
  /// \return Status error if actor creation fails, likely due to raylet failure.
  Status CreateActor(const RayFunction &function,
                     const std::vector<std::unique_ptr<TaskArg>> &args,
                     const ActorCreationOptions &actor_creation_options,
                     const std::string &extension_data, ActorID *actor_id);
  /// 创建一个放置组
  /// 参数[in] function 生成这个放置组对象的远程函数
  /// 参数[in] placement_group_creation_options 任务的选项
  /// 参数[out] placement_group_id 生成的放置组的id
  /// 这个可以用来调节节点中的actor
  /// 返回 状态 如果创建失败，则返回error，可能是由于raylet失败
  ///
  /// Create a placement group.
  ///
  /// \param[in] function The remote function that generates the placement group object.
  /// \param[in] placement_group_creation_options Options for this placement group
  /// creation task.
  /// \param[out] placement_group_id ID of the created placement group.
  /// This can be used to shedule actor in node
  /// \return Status error if placement group
  /// creation fails, likely due to raylet failure.
  Status CreatePlacementGroup(
      const PlacementGroupCreationOptions &placement_group_creation_options,
      PlacementGroupID *placement_group_id);

  /// 移除一个放置组，注意，这个操作是同步的
  /// 参数[in] placement_group_id 要移除的放置组的id
  /// 返回 成功时返回状态ok，Gcs服务超时时返回TimedOut，放置组已被移除或不存在时返回NotFound 
  /// Remove a placement group. Note that this operation is synchronous.
  ///
  /// \param[in] placement_group_id The id of a placement group to remove.
  /// \return Status OK if succeed. TimedOut if request to GCS server times out.
  /// NotFound if placement group is already removed or doesn't exist.
  Status RemovePlacementGroup(const PlacementGroupID &placement_group_id);

  /// 等待一个放置组直到其异步就绪
  /// 一旦放置组创建或超时就返回。  
  /// 参数 placement_group_id 要等待的放置组的id
  /// 参数 timeout_seconds 超时的秒数，超过多少秒就返回
  /// 返回 若放置组已被创建，则返回OK，若到GCS服务器的请求超时，返回 TimedOut
  /// 若放置组已经被移除或不存在，则返回NotFound
  
  /// Wait for a placement group until ready asynchronously.
  /// Returns once the placement group is created or the timeout expires.
  ///
  /// \param placement_group The id of a placement group to wait for.
  /// \param timeout_seconds Timeout in seconds.
  /// \return Status OK if the placement group is created. TimedOut if request to GCS
  /// server times out. NotFound if placement group is already removed or doesn't exist.
  Status WaitPlacementGroupReady(const PlacementGroupID &placement_group_id,
                                 int timeout_seconds);

  /// 提交一个actor任务
  /// 参数[in] caller_id 任务提交者的id
  /// 参数[in] actor_handle actor的handle
  /// 参数[in] function 要执行的远程函数
  /// 参数[in] args 此任务的参数
  /// 参数[in] task_options 此任务的选项
  /// 返回 此任务返回的ObjectRefs
  /// Submit an actor task.
  ///
  /// \param[in] caller_id ID of the task submitter.
  /// \param[in] actor_handle Handle to the actor.
  /// \param[in] function The remote function to execute.
  /// \param[in] args Arguments of this task.
  /// \param[in] task_options Options for this task.
  /// \return ObjectRefs returned by this task.
  std::vector<rpc::ObjectReference> SubmitActorTask(
      const ActorID &actor_id, const RayFunction &function,
      const std::vector<std::unique_ptr<TaskArg>> &args, const TaskOptions &task_options);

  /// 让一个actor立刻离开，不需要完成现在的任务
  /// 参数[in] actir_id 要杀死的actor的id
  /// 参数[in] force_kill 是否通过杀死worker来强制杀死actor。
  /// 参数[in] no_restart 若为true，被杀死的actor将不再重启
  /// 参数[out] 状态
  
  /// Tell an actor to exit immediately, without completing outstanding work.
  ///
  /// \param[in] actor_id ID of the actor to kill.
  /// \param[in] force_kill Whether to force kill an actor by killing the worker.
  /// \param[in] no_restart If set to true, the killed actor will not be
  /// restarted anymore.
  /// \param[out] Status
  Status KillActor(const ActorID &actor_id, bool force_kill, bool no_restart);

  /// 停止与给定对象ID关联的任务。 
  /// 参数[in] object_id 要杀死的对象id（必须是non-actor任务）
  /// 参数[in] force_kill 是否通过杀死worker来强制杀死一个任务
  /// 参数[in] recursive   是否取消由要取消的任务提交的任务
  /// 参数[out] 状态
  /// Stops the task associated with the given Object ID.
  ///
  /// \param[in] object_id of the task to kill (must be a Non-Actor task)
  /// \param[in] force_kill Whether to force kill a task by killing the worker.
  /// \param[in] recursive Whether to cancel tasks submitted by the task to cancel.
  /// \param[out] Status
  Status CancelTask(const ObjectID &object_id, bool force_kill, bool recursive);

  /// 减少此actor的引用计数。当对actor句柄的引用被销毁时，应该被语言前端调用。
  /// 参数[in] actor_id 要减少引用计数的actor的id
  /// Decrease the reference count for this actor. Should be called by the
  /// language frontend when a reference to the ActorHandle destroyed.
  ///
  /// \param[in] actor_id The actor ID to decrease the reference count for.
  void RemoveActorHandleReference(const ActorID &actor_id);


  /// 从序列化的字符串中添加一个actor句柄。
  /// 当另一个任务或actor给我们一个actor句柄时，应该调用这个函数。
  /// 即使我们已经有了同一个actor的句柄，也可以调用它。  
  /// 参数[in]   serialized 序列化的actor句柄。
  /// 参数[in] outer_object_id 包含序列化的actor句柄(如果有的话)的对象ID。 
  /// 返回 序列化句柄的ActorID
  /// Add an actor handle from a serialized string.
  ///
  /// This should be called when an actor handle is given to us by another task
  /// or actor. This may be called even if we already have a handle to the same
  /// actor.
  ///
  /// \param[in] serialized The serialized actor handle.
  /// \param[in] outer_object_id The object ID that contained the serialized
  /// actor handle, if any.
  /// \return The ActorID of the deserialized handle.
  ActorID DeserializeAndRegisterActorHandle(const std::string &serialized,
                                            const ObjectID &outer_object_id);

  /// 序列化一个actor句柄
  /// 当把一个actor句柄传递给另一个任务或actor时调用此函数                        
  /// 参数[in] actor_id 要序列化的actor句柄的id
  /// 参数[out] 序列化的句柄
  /// 参数[out] 用于跟踪对actor句柄的引用的ID。 
  /// 如果语言前端中序列化的actor句柄被存储在一个对象中，那么它必须被记录在worker的referenceccounter中。
  /// 返回 Status::Invalid 如果我们没有指定的句柄。 
  /// Serialize an actor handle.
  ///
  /// This should be called when passing an actor handle to another task or
  /// actor.
  ///
  /// \param[in] actor_id The ID of the actor handle to serialize.
  /// \param[out] The serialized handle.
  /// \param[out] The ID used to track references to the actor handle. If the
  /// serialized actor handle in the language frontend is stored inside an
  /// object, then this must be recorded in the worker's ReferenceCounter.
  /// \return Status::Invalid if we don't have the specified handle.
  Status SerializeActorHandle(const ActorID &actor_id, std::string *output,
                              ObjectID *actor_handle_id) const;

  /// 
  /// 与任务执行相关的公共方法。不应该被driver进程使用。
  /// Public methods related to task execution. Should not be used by driver processes.
  ///

  const ActorID &GetActorId() const { return actor_id_; }

  // 获取该工作人员可用的资源id(由raylet分配)。 
  // Get the resource IDs available to this worker (as assigned by the raylet).
  const ResourceMappingType GetResourceIDs() const;

  /// 创建一个概要事件,并引用core worker的分析器
  /// Create a profile event with a reference to the core worker's profiler.
  std::unique_ptr<worker::ProfileEvent> CreateProfileEvent(const std::string &event_type);

  int64_t GetNumTasksSubmitted() const {
    return direct_task_submitter_->GetNumTasksSubmitted();
  }

  int64_t GetNumLeasesRequested() const {
    return direct_task_submitter_->GetNumLeasesRequested();
  }

 public:
  /// 为执行任务分配返回对象。
  /// 调用者应该写入分配的缓冲区的数据缓冲区,然后调用SealReturnObject()来封住它。
  /// 为了避免死锁,调用者应该一次分配并封一个单个对象。 
  /// 
  /// 参数[in] object_id 返回值的对象id
  /// 参数[in] data_size 返回值的大小
  /// 参数[in] metadata 返回值的元数据缓冲区。
  /// 参数[in] contained_object_id 在每个返回对象中序列化ID。
  /// 参数[in][out] task_output_inlined_bytes 存储任务中所有inlined对象的总大小。
  /// 它被用来决定当前对象是否应该被输入。如果当前对象在内线中,task_output_inlined_bytes将会被更新。 
  /// 参数[out] return_object RayObject包含用来写入结果的缓冲区。
  /// 返回 状态
  /// Allocate the return object for an executing task. The caller should write into the
  /// data buffer of the allocated buffer, then call SealReturnObject() to seal it.
  /// To avoid deadlock, the caller should allocate and seal a single object at a time.
  ///
  /// \param[in] object_id Object ID of the return value.
  /// \param[in] data_size Size of the return value.
  /// \param[in] metadata Metadata buffer of the return value.
  /// \param[in] contained_object_id ID serialized within each return object.
  /// \param[in][out] task_output_inlined_bytes Store the total size of all inlined
  /// objects of a task. It is used to decide if the current object should be inlined. If
  /// the current object is inlined, the task_output_inlined_bytes will be updated.
  /// \param[out] return_object RayObject containing buffers to write results into.
  /// \return Status.
  Status AllocateReturnObject(const ObjectID &object_id, const size_t &data_size,
                              const std::shared_ptr<Buffer> &metadata,
                              const std::vector<ObjectID> &contained_object_id,
                              int64_t &task_output_inlined_bytes,
                              std::shared_ptr<RayObject> *return_object);
  /// 为执行任务封回对象。调用者应该已经写入了数据缓冲区。 
  /// 参数[in] return_id 返回值的object id
  /// 参数[in] return_object 包含缓冲区写入信息的RayObject。 
  /// 返回 状态
  /// Seal a return object for an executing task. The caller should already have
  /// written into the data buffer.
  ///
  /// \param[in] return_id Object ID of the return value.
  /// \param[in] return_object RayObject containing the buffer written info.
  /// \return Status.
  Status SealReturnObject(const ObjectID &return_id,
                          std::shared_ptr<RayObject> return_object);

  /// 获取一个actor的句柄
  /// 注意：只有当我们确定actor句柄存在时才能调用这个函数
  /// 注意：这个函数获得的actor_handle不应该存储在任何地方,因为该方法将原始指针返回到一个独特的指针指的地方。 
  /// 参数[in] actor_id 要获取的actor句柄
  /// 返回   如果我们没有这个actor的句柄时，返回Status::Invalid
  /// Get a handle to an actor.
  ///
  /// NOTE: This function should be called ONLY WHEN we know actor handle exists.
  /// NOTE: The actor_handle obtained by this function should not be stored anywhere
  /// because this method returns the raw pointer to what a unique pointer points to.
  ///
  /// \param[in] actor_id The actor handle to get.
  /// \return Status::Invalid if we don't have this actor handle.
  std::shared_ptr<const ActorHandle> GetActorHandle(const ActorID &actor_id) const;

  /// 根据名称获取一个actor的句柄
  /// 注意：该函数获得的actor_handle不应该存储在任何地方。
  /// 参数[in] name 要获取句柄的actor的名字
  /// 参数[in] ray_namespace 请求的actor的名称空间。 
  /// 参数[out] actor_handle 所请求的actor的句柄
  /// 返回 
  /// Get a handle to a named actor.
  ///
  /// NOTE: The actor_handle obtained by this function should not be stored anywhere.
  ///
  /// \param[in] name The name of the actor whose handle to get.
  /// \param[in] ray_namespace The namespace of the requested actor.
  /// \param[out] actor_handle A handle to the requested actor.
  /// \return The shared_ptr to the actor handle if found, nullptr otherwise.
  /// The second pair contains the status of getting a named actor handle.
  std::pair<std::shared_ptr<const ActorHandle>, Status> GetNamedActorHandle(
      const std::string &name, const std::string &ray_namespace);

  /// 返回当前在系统中指定的命名过的actor的列表
  /// 每个actor都以<namespace, name>对的形式返回
  /// 这包括正在等待或重新启动的actor
  /// 参数 all_namespaces 是否包括来自所有命名空间的actor。
  /// 返回 <namespace,name>对和状态的列表。 
  /// Returns a list of the named actors currently in the system.
  ///
  /// Each actor is returned as a pair of <namespace, name>.
  /// This includes actors that are pending placement or being restarted.
  ///
  /// \param all_namespaces Whether or not to include actors from all namespaces.
  /// \return The list of <namespace, name> pairs and a status.
  std::pair<std::vector<std::pair<std::string, std::string>>, Status> ListNamedActors(
      bool all_namespaces);

  /// 
  /// 下面的方法是core worker的gRPC服务器的处理程序,它遵循一个宏观生成的调用惯例。
  /// 这些在io_service_和post工作上执行到适当的事件循环。
  /// The following methods are handlers for the core worker's gRPC server, which follow
  /// a macro-generated call convention. These are executed on the io_service_ and
  /// post work to the appropriate event loop.
  ///

  /// 实现gRPC服务器处理程序
  /// Implements gRPC server handler.
  void HandlePushTask(const rpc::PushTaskRequest &request, rpc::PushTaskReply *reply,
                      rpc::SendReplyCallback send_reply_callback) override;

  /// 实现gRPC服务器处理程序
  /// Implements gRPC server handler.
  void HandleStealTasks(const rpc::StealTasksRequest &request,
                        rpc::StealTasksReply *reply,
                        rpc::SendReplyCallback send_reply_callback) override;
  
  /// 实现gRPC服务器处理程序
  /// Implements gRPC server handler.
  void HandleDirectActorCallArgWaitComplete(
      const rpc::DirectActorCallArgWaitCompleteRequest &request,
      rpc::DirectActorCallArgWaitCompleteReply *reply,
      rpc::SendReplyCallback send_reply_callback) override;
  
  /// 实现gRPC服务器处理程序
  /// Implements gRPC server handler.
  void HandleGetObjectStatus(const rpc::GetObjectStatusRequest &request,
                             rpc::GetObjectStatusReply *reply,
                             rpc::SendReplyCallback send_reply_callback) override;

  /// 实现gRPC服务器处理程序
  /// Implements gRPC server handler.
  void HandleWaitForActorOutOfScope(const rpc::WaitForActorOutOfScopeRequest &request,
                                    rpc::WaitForActorOutOfScopeReply *reply,
                                    rpc::SendReplyCallback send_reply_callback) override;

  /// 实现gRPC服务器处理程序
  // Implements gRPC server handler.
  void HandlePubsubLongPolling(const rpc::PubsubLongPollingRequest &request,
                               rpc::PubsubLongPollingReply *reply,
                               rpc::SendReplyCallback send_reply_callback) override;

  /// 实现gRPC服务器处理程序
  // Implements gRPC server handler.
  void HandlePubsubCommandBatch(const rpc::PubsubCommandBatchRequest &request,
                                rpc::PubsubCommandBatchReply *reply,
                                rpc::SendReplyCallback send_reply_callback) override;

  /// 实现gRPC服务器处理程序
  // Implements gRPC server handler.
  void HandleUpdateObjectLocationBatch(
      const rpc::UpdateObjectLocationBatchRequest &request,
      rpc::UpdateObjectLocationBatchReply *reply,
      rpc::SendReplyCallback send_reply_callback) override;

  /// 实现gRPC服务器处理程序
  /// Implements gRPC server handler.
  void HandleGetObjectLocationsOwner(const rpc::GetObjectLocationsOwnerRequest &request,
                                     rpc::GetObjectLocationsOwnerReply *reply,
                                     rpc::SendReplyCallback send_reply_callback) override;

  /// 实现gRPC服务器处理程序
  /// Implements gRPC server handler.
  void HandleKillActor(const rpc::KillActorRequest &request, rpc::KillActorReply *reply,
                       rpc::SendReplyCallback send_reply_callback) override;

  /// 实现gRPC服务器处理程序
  /// Implements gRPC server handler.
  void HandleCancelTask(const rpc::CancelTaskRequest &request,
                        rpc::CancelTaskReply *reply,
                        rpc::SendReplyCallback send_reply_callback) override;

  /// 实现gRPC服务器处理程序
  /// Implements gRPC server handler.
  void HandleRemoteCancelTask(const rpc::RemoteCancelTaskRequest &request,
                              rpc::RemoteCancelTaskReply *reply,
                              rpc::SendReplyCallback send_reply_callback) override;

  /// 实现gRPC服务器处理程序
  /// Implements gRPC server handler.
  void HandlePlasmaObjectReady(const rpc::PlasmaObjectReadyRequest &request,
                               rpc::PlasmaObjectReadyReply *reply,
                               rpc::SendReplyCallback send_reply_callback) override;

  /// 从core worker处获取统计数据
  /// Get statistics from core worker.
  void HandleGetCoreWorkerStats(const rpc::GetCoreWorkerStatsRequest &request,
                                rpc::GetCoreWorkerStatsReply *reply,
                                rpc::SendReplyCallback send_reply_callback) override;

  /// 在当前worker上触发本地GC
  /// Trigger local GC on this worker.
  void HandleLocalGC(const rpc::LocalGCRequest &request, rpc::LocalGCReply *reply,
                     rpc::SendReplyCallback send_reply_callback) override;

  /// 对基于python的IO worker运行请求 
  // Run request on an python-based IO worker
  void HandleRunOnUtilWorker(const rpc::RunOnUtilWorkerRequest &request,
                             rpc::RunOnUtilWorkerReply *reply,
                             rpc::SendReplyCallback send_reply_callback) override;

  /// 将对象释放到外部存储
  // Spill objects to external storage.
  void HandleSpillObjects(const rpc::SpillObjectsRequest &request,
                          rpc::SpillObjectsReply *reply,
                          rpc::SendReplyCallback send_reply_callback) override;

  /// 给所有者引用添加外部URL。 
  // Add spilled URL to owned reference.
  void HandleAddSpilledUrl(const rpc::AddSpilledUrlRequest &request,
                           rpc::AddSpilledUrlReply *reply,
                           rpc::SendReplyCallback send_reply_callback) override;

  /// 从外部存储恢复对象
  // Restore objects from external storage.
  void HandleRestoreSpilledObjects(const rpc::RestoreSpilledObjectsRequest &request,
                                   rpc::RestoreSpilledObjectsReply *reply,
                                   rpc::SendReplyCallback send_reply_callback) override;

  // 从外部存储删除对象
  // Delete objects from external storage.
  void HandleDeleteSpilledObjects(const rpc::DeleteSpilledObjectsRequest &request,
                                  rpc::DeleteSpilledObjectsReply *reply,
                                  rpc::SendReplyCallback send_reply_callback) override;

  // 使当前worker退出
  // 如果当前core worker拥有任何对象，那么这个请求会失败
  // Make the this worker exit.
  // This request fails if the core worker owns any object.
  void HandleExit(const rpc::ExitRequest &request, rpc::ExitReply *reply,
                  rpc::SendReplyCallback send_reply_callback) override;

  // 将本地worker设置为某个对象的所有者
  // 由借用者的worker发出请求，由所有者的worker执行
  // Set local worker as the owner of object.
  // Request by borrower's worker, execute by owner's worker.
  void HandleAssignObjectOwner(const rpc::AssignObjectOwnerRequest &request,
                               rpc::AssignObjectOwnerReply *reply,
                               rpc::SendReplyCallback send_reply_callback) override;

  ///
  /// 与异步actor调用相关的公共方法。
  /// 当演员使用异步模式时,该选项只能当actor是direct actor且使用异步模式。
  /// Public methods related to async actor call. This should only be used when
  /// the actor is (1) direct actor and (2) using asyncio mode.
  ///

  /// 阻塞当前fiber，直到事件被触发
  /// Block current fiber until event is triggered.
  void YieldCurrentFiber(FiberEvent &event);

  /// 将由客户端执行的回调
  /// The callback expected to be implemented by the client.
  using SetResultCallback =
      std::function<void(std::shared_ptr<RayObject>, ObjectID object_id, void *)>;

  /// 执行从object store中异步get
  /// 参数[in] object_id 要get的对象id
  /// 参数[in] success_callback 使用结果对象的回调
  /// 参数[in] python_future 被传递到SetResultCallback的void* object
  /// 返回 空
  /// Perform async get from the object store.
  ///
  /// \param[in] object_id The id to call get on.
  /// \param[in] success_callback The callback to use the result object.
  /// \param[in] python_future the void* object to be passed to SetResultCallback
  /// \return void
  void GetAsync(const ObjectID &object_id, SetResultCallback success_callback,
                void *python_future);

  // 获取序列化的工作配置。
  // Get serialized job configuration.
  const rpc::JobConfig &GetJobConfig() const;

  // 获取gcs客户端
  // Get gcs_client
  std::shared_ptr<gcs::GcsClient> GetGcsClient() const;

  /// 判断core worker是否在退出进程中
  /// 若在，返回true
  /// Return true if the core worker is in the exit process.
  bool IsExiting() const;

 private:
  void SetCurrentTaskId(const TaskID &task_id);

  void SetActorId(const ActorID &actor_id);

  /// 运行io_service_事件循环。这应该在后台线程中调用。
  /// Run the io_service_ event loop. This should be called in a background thread.
  void RunIOService();

  /// (仅在worker mode可以使用)让worker退出，这是关闭worker的入口
  /// (WORKER mode only) Exit the worker. This is the entrypoint used to shutdown a
  /// worker.
  void Exit(rpc::WorkerExitType exit_type,
            const std::shared_ptr<LocalMemoryBuffer> &creation_task_exception_pb_bytes =
                nullptr);

  /// 将这个worker或者driver注册到gcs
  /// Register this worker or driver to GCS.
  void RegisterToGcs();

  /// 检测raylet是否失败了，如果失败了，就关闭它
  /// Check if the raylet has failed. If so, shutdown.
  void CheckForRayletFailure();

  /// 内部bookkeeping的心跳
  /// Heartbeat for internal bookkeeping.
  void InternalHeartbeat();

  /// 在对象状态应答中填充对象的助手方法。 
  /// Helper method to fill in object status reply given an object.
  void PopulateObjectStatus(const ObjectID &object_id, std::shared_ptr<RayObject> obj,
                            rpc::GetObjectStatusReply *reply);

  ///
  /// 与任务提交相关的私有方法。
  /// Private methods related to task submission.
  ///

  /// 增加该对象ID的本地引用计数。
  /// 当创建新的引用时,应该由语言前端调用。
  /// 参数[in] object_id 对象ID增加引用计数。 
  /// 参数[in] call_site 来自语言前端的调用
  /// Increase the local reference count for this object ID. Should be called
  /// by the language frontend when a new reference is created.
  ///
  /// \param[in] object_id The object ID to increase the reference count for.
  /// \param[in] call_site The call site from the language frontend.
  void AddLocalReference(const ObjectID &object_id, std::string call_site) {
    reference_counter_->AddLocalReference(object_id, call_site);
  }

  /// 根据给定的taskid中止他的子任务
  /// 参数[in] task_id 父任务的id
  /// 参数[in] force_kill 是否通过杀死worker来强制杀死一个任务
  /// Stops the children tasks from the given TaskID
  ///
  /// \param[in] task_id of the parent task
  /// \param[in] force_kill Whether to force kill a task by killing the worker.
  Status CancelChildren(const TaskID &task_id, bool force_kill);

  /// 
  /// 与任务执行相关的私有方法。不应被驱动程序使用。 
  /// Private methods related to task execution. Should not be used by driver processes.
  ///

  /// 执行一个任务
  /// 参数 spec[in] task_spec 任务规范
  /// 参数 spec[in] resource_ids 分配给这个worker的资源的资源id。
  ///               如果为nullptr,重用以前分配的资源。
  /// 参数 results[out] return_objects 结果对象应该通过值返回(不是通过plasma)。 
  /// 参数 results[out] borrowed_refs 这个任务(或嵌套任务)仍在借用的引用。
  ///		            这包括我们在其参数和递归中传递给任务的所有对象的id和那些对象中包含的对象id
  /// 返回 状态
  ///
  /// Execute a task.
  ///
  /// \param spec[in] task_spec Task specification.
  /// \param spec[in] resource_ids Resource IDs of resources assigned to this
  ///                 worker. If nullptr, reuse the previously assigned
  ///                 resources.
  /// \param results[out] return_objects Result objects that should be returned
  ///                     by value (not via plasma).
  /// \param results[out] borrowed_refs Refs that this task (or a nested task)
  ///                     was or is still borrowing. This includes all
  ///                     objects whose IDs we passed to the task in its
  ///                     arguments and recursively, any object IDs that were
  ///                     contained in those objects.
  /// \return Status.
  Status ExecuteTask(const TaskSpecification &task_spec,
                     const std::shared_ptr<ResourceMappingType> &resource_ids,
                     std::vector<std::shared_ptr<RayObject>> *return_objects,
                     ReferenceCounter::ReferenceTableProto *borrowed_refs,
                     bool *is_application_level_error);

  /// 执行本地模式任务(运行正常的执行任务) 
  /// 参数 spec[in] task_spec 任务规范
  /// Execute a local mode task (runs normal ExecuteTask)
  ///
  /// \param spec[in] task_spec Task specification.
  std::vector<rpc::ObjectReference> ExecuteTaskLocalMode(
      const TaskSpecification &task_spec, const ActorID &actor_id = ActorID::Nil());

  /// 本地模式的KillActor API
  /// KillActor API for a local mode.
  Status KillActorLocalMode(const ActorID &actor_id);

  /// 为本地模式的指定actor获取一个句柄
  /// Get a handle to a named actor for local mode.
  std::pair<std::shared_ptr<const ActorHandle>, Status> GetNamedActorHandleLocalMode(
      const std::string &name);

  /// 在本地模式中获取所有被命名的actor。
  /// Get all named actors in local mode.
  std::pair<std::vector<std::pair<std::string, std::string>>, Status>
  ListNamedActorsLocalMode();

  /// 获取执行器的任务参数的值。  
  /// 从本地plasma库检索值,如果值是内部的，则从任务空间获取。
  ///
  /// 这也可以在引用计数器中添加局部引用的情况下,把所有的plasma参数和目标都集中在一个嵌条的参数中。
  /// 这是为了确保我们有需要检索该对象的值时所需要的对象所有者的地址。
  /// 它还确保当任务完成时,我们可以检索任何仍然被这个过程借用的对象的元数据。
  /// 一旦任务完成,IDs就应该不用固定。
  /// 参数 spec[in] task 任务规范
  /// 参数 args[out] args RayObjects的参数数据
  /// 参数 args[out] arg_reference_ids ObjectIDs与每一个参考参数对应。
  ///                该参数的这个向量的长度与args相同,值参数将具有objective d::Nil()。
  ///                //TODO(edoakes):这是一种必要的方法,因为我们有不同的序列化路径,
  ///                在Python中引用值和引用参数。这应该在理想的情况下得到更好的处理。
  /// 参数 args[out] pinned_ids 一旦任务完成执行,就应该不被固定。
  ///                这个向量将被填充到所有被引用传递的参数id,以及在任务规范中包含的参数中包含的任何目标。 
  /// 返回 如果无法检索到值，则返回Error
  /// Get the values of the task arguments for the executor. Values are
  /// retrieved from the local plasma store or, if the value is inlined, from
  /// the task spec.
  ///
  /// This also pins all plasma arguments and ObjectIDs that were contained in
  /// an inlined argument by adding a local reference in the reference counter.
  /// This is to ensure that we have the address of the object's owner, which
  /// is needed to retrieve the value. It also ensures that when the task
  /// completes, we can retrieve any metadata about objects that are still
  /// being borrowed by this process. The IDs should be unpinned once the task
  /// completes.
  ///
  /// \param spec[in] task Task specification.
  /// \param args[out] args Argument data as RayObjects.
  /// \param args[out] arg_reference_ids ObjectIDs corresponding to each by
  ///                  reference argument. The length of this vector will be
  ///                  the same as args, and by value arguments will have
  ///                  ObjectID::Nil().
  ///                  // TODO(edoakes): this is a bit of a hack that's necessary because
  ///                  we have separate serialization paths for by-value and by-reference
  ///                  arguments in Python. This should ideally be handled better there.
  /// \param args[out] pinned_ids ObjectIDs that should be unpinned once the
  ///                  task completes execution.  This vector will be populated
  ///                  with all argument IDs that were passed by reference and
  ///                  any ObjectIDs that were included in the task spec's
  ///                  inlined arguments.
  /// \return Error if the values could not be retrieved.
  Status GetAndPinArgsForExecutor(const TaskSpecification &task,
                                  std::vector<std::shared_ptr<RayObject>> *args,
                                  std::vector<rpc::ObjectReference> *arg_refs,
                                  std::vector<ObjectID> *pinned_ids);

  /// 处理等待对象回收的订阅消息
  /// 一旦对象被回收,对象回收信息将会被发布。
  /// Process a subscribe message for wait for object eviction.
  /// The object eviction message will be published once the object
  /// needs to be evicted.
  void ProcessSubscribeForObjectEviction(
      const rpc::WorkerObjectEvictionSubMessage &message);

  /// 处理等待ref删除的订阅消息。 
  /// 它用于ref计数协议。当借用者停止使用引用时,消息将被发布给所有者。
  /// Process a subscribe message for wait for ref removed.
  /// It is used for the ref counting protocol. When the borrower
  /// stops using the reference, the message will be published to the owner.
  void ProcessSubscribeForRefRemoved(const rpc::WorkerRefRemovedSubMessage &message);

  /// 处理获取对象位置的订阅消息。 
  /// 由于core worker拥有对象目录,因此有各种不同的raylets来订阅这个对象目录。 
  /// Process a subscribe message for object locations.
  /// Since core worker owns the object directory, there are various raylets
  /// that subscribe this object directory.
  void ProcessSubscribeObjectLocations(
      const rpc::WorkerObjectLocationsSubMessage &message);

  using Commands = ::google::protobuf::RepeatedPtrField<rpc::Command>;

  /// 处理从订阅方接收的订阅消息。 
  /// Process the subscribe message received from the subscriber.
  void ProcessSubscribeMessage(const rpc::SubMessage &sub_message,
                               rpc::ChannelType channel_type, const std::string &key_id,
                               const NodeID &subscriber_id);

  /// 一个单个的端点来处理不同类型的pubsub命令
  /// Pubsub命令用于批处理,并包含各种订阅/ 非订阅消息 
  /// A single endpoint to process different types of pubsub commands.
  /// Pubsub commands are coming as a batch and contain various subscribe / unbsubscribe
  /// messages.
  void ProcessPubsubCommands(const Commands &commands, const NodeID &subscriber_id);

  void AddObjectLocationOwner(const ObjectID &object_id, const NodeID &node_id);

  void RemoveObjectLocationOwner(const ObjectID &object_id, const NodeID &node_id);

  /// 返回信息是否被送到了错误的worker
  /// 正确的错误回复是自动发送的。在这种情况下,我们希望新员工拒绝对旧的消息。 
  /// 当一个worker死后,消息就会结束在错误的worker而且一个新的worker会占据旧worker的地方。
  /// 在这种情况下，我们希望新的worker拒绝对旧worker的消息
  /// Returns whether the message was sent to the wrong worker. The right error reply
  /// is sent automatically. Messages end up on the wrong worker when a worker dies
  /// and a new one takes its place with the same place. In this situation, we want
  /// the new worker to reject messages meant for the old one.
  bool HandleWrongRecipient(const WorkerID &intended_worker_id,
                            rpc::SendReplyCallback send_reply_callback) {
    if (intended_worker_id != worker_context_.GetWorkerID()) {
      std::ostringstream stream;
      stream << "Mismatched WorkerID: ignoring RPC for previous worker "
             << intended_worker_id
             << ", current worker ID: " << worker_context_.GetWorkerID();
      auto msg = stream.str();
      RAY_LOG(ERROR) << msg;
      send_reply_callback(Status::Invalid(msg), nullptr, nullptr);
      return true;
    } else {
      return false;
    }
  }

  /// 处理如果一个raylet节点从集群中删除的情况
  /// Handler if a raylet node is removed from the cluster.
  void OnNodeRemoved(const NodeID &node_id);

  /// ?处理我们拥有的对象溢出的请求,我们从主文件复制到溢出的主文件中。
  /// Request the spillage of an object that we own from the primary that hosts
  /// the primary copy to spill.
  void SpillOwnedObject(const ObjectID &object_id, const std::shared_ptr<RayObject> &obj,
                        std::function<void()> callback);

  const CoreWorkerOptions options_;

  /// 回调以获得当前语言(例如:,Python)的call site 
  /// Callback to get the current language (e.g., Python) call site.
  std::function<void(std::string *)> get_call_site_;

  // 简便方法来获取当前语言的call site
  // Convenience method to get the current language call site.
  std::string CurrentCallSite() {
    std::string call_site;
    if (get_call_site_ != nullptr) {
      get_call_site_(&call_site);
    }
    return call_site;
  }

  /// worker的共享状态，包括进程级别和线程级状态。 
  /// TODO(edoakes):我们应该将process-level状态移动到这个类中,并使其成为线程上下文。
  /// Shared state of the worker. Includes process-level and thread-level state.
  /// TODO(edoakes): we should move process-level state into this class and make
  /// this a ThreadContext.
  WorkerContext worker_context_;

  /// 由主线程执行的当前任务的id。
  /// 如果有多个线程，他们将在工作环境中存储一个thread-local 任务ID
  /// The ID of the current task being executed by the main thread. If there
  /// are multiple threads, they will have a thread-local task ID stored in the
  /// worker context.
  TaskID main_thread_task_id_ GUARDED_BY(mutex_);

  /// 处理IO事件的事件循环。例如,异步GCS操作。 
  /// Event loop where the IO events are handled. e.g. async GCS operations.
  instrumented_io_context io_service_;

  /// 保持io_service_活着。 
  /// Keeps the io_service_ alive.
  boost::asio::io_service::work io_work_;

  /// 共享客户端调用管理器。
  /// Shared client call manager.
  std::unique_ptr<rpc::ClientCallManager> client_call_manager_;

  /// 共享core worker客户端池
  /// Shared core worker client pool.
  std::shared_ptr<rpc::CoreWorkerClientPool> core_worker_client_pool_;

  /// 计时器定期运行功能。 
  /// The runner to run function periodically.
  PeriodicalRunner periodical_runner_;

  /// 用于接收要执行的任务的RPC服务器。 
  /// RPC server used to receive tasks to execute.
  std::unique_ptr<rpc::GrpcServer> core_worker_server_;

  /// 我们的RPC服务器的地址
  /// Address of our RPC server.
  rpc::Address rpc_address_;

  /// 这个worker是否连接到raylet和GCS。
  /// Whether or not this worker is connected to the raylet and GCS.
  bool connected_ = false;

  std::pair<std::string, int> gcs_server_address_ GUARDED_BY(gcs_server_address_mutex_) =
      std::make_pair<std::string, int>("", 0);
  /// 用于保护访问'gcs_server_address_'
  /// To protect accessing the `gcs_server_address_`.
  absl::Mutex gcs_server_address_mutex_;
  std::unique_ptr<GcsServerAddressUpdater> gcs_server_address_updater_;

  // ?通过core worker接口连接到GCS的客户端
  // Client to the GCS shared by core worker interfaces.
  std::shared_ptr<gcs::GcsClient> gcs_client_;

  // ?通过core worker接口连接到raylet的客户端
  // 这需要作为一个shared_ptr来直接调用,因为我们可以通过一个客户端租赁多个worker,
  // 我们需要保持连接,直到我们归还了所有的worker。 
  // Client to the raylet shared by core worker interfaces. This needs to be a
  // shared_ptr for direct calls because we can lease multiple workers through
  // one client, and we need to keep the connection alive until we return all
  // of the workers.
  std::shared_ptr<raylet::RayletClient> local_raylet_client_;

  // 运行boost::asio service来处理IO事件的线程
  // Thread that runs a boost::asio service to process IO events.
  std::thread io_thread_;

  // 跟踪对象ID的引用计数。 
  // Keeps track of object ID reference counts.
  std::shared_ptr<ReferenceCounter> reference_counter_;

  ///
  /// 用于存储和检索对象的字段。 
  /// Fields related to storing and retrieving objects.
  ///

  /// 用于返回对象的内存存储库。 
  /// In-memory store for return objects.
  std::shared_ptr<CoreWorkerMemoryStore> memory_store_;

  /// Plasma 存储的接口
  /// Plasma store interface.
  std::shared_ptr<CoreWorkerPlasmaStoreProvider> plasma_store_provider_;

  std::unique_ptr<FutureResolver> future_resolver_;

  ///
  /// 与任务提交相关的字段。 
  /// Fields related to task submission.
  ///

  // 跟踪当前正在等待的任务。 
  // Tracks the currently pending tasks.
  std::shared_ptr<TaskManager> task_manager_;

  // 用于将任务直接提交给其他参与者的接口。
  // Interface to submit tasks directly to other actors.
  std::shared_ptr<CoreWorkerDirectActorTaskSubmitter> direct_actor_submitter_;

  // 从其他raylets/worker中发布对象状态的类。 
  // A class to publish object status from other raylets/workers.
  std::unique_ptr<pubsub::Publisher> object_info_publisher_;

  // 从其他raylets / worker中订阅对象状态的类。 
  // A class to subscribe object status from other raylets/workers.
  std::unique_ptr<pubsub::Subscriber> object_info_subscriber_;

  // 直接向租赁的worker提交non-actor任务的接口。 
  // Interface to submit non-actor tasks directly to leased workers.
  std::unique_ptr<CoreWorkerDirectTaskSubmitter> direct_task_submitter_;

  /// 管理存储在远程plasma节点中的对象的恢复。 
  /// Manages recovery of objects stored in remote plasma nodes.
  std::unique_ptr<ObjectRecoveryManager> object_recovery_manager_;

  ///
  /// 与actor句柄相关的字段
  /// Fields related to actor handles.
  ///

  /// 管理actor的句柄们的接口
  /// Interface to manage actor handles.
  std::unique_ptr<ActorManager> actor_manager_;

  ///
  /// 与任务执行相关的字段。 
  /// Fields related to task execution.
  ///

  /// ?保护周围的字段。这只应该在短时间内举行。 
  /// Protects around accesses to fields below. This should only ever be held
  /// for short-running periods of time.
  mutable absl::Mutex mutex_;

  /// 我们的actorID。如果是nil,那么我们只执行无状态任务
  /// Our actor ID. If this is nil, then we execute only stateless tasks.
  ActorID actor_id_ GUARDED_BY(mutex_);

  /// 当前执行任务规范。我们必须单独跟踪这个,
  /// 因为我们无法从GetCoreWorkerStats()访问线程本地的工作上下文
  /// The currently executing task spec. We have to track this separately since
  /// we cannot access the thread-local worker contexts from GetCoreWorkerStats()
  TaskSpecification current_task_ GUARDED_BY(mutex_);

  /// 在Web UI上显示的键值对。 
  /// Key value pairs to be displayed on Web UI.
  std::unordered_map<std::string, std::string> webui_display_ GUARDED_BY(mutex_);

  /// 包含类名，参数，为actor建设的kwargs的Actor title
  /// Actor title that consists of class name, args, kwargs for actor construction.
  std::string actor_title_ GUARDED_BY(mutex_);

  /// 被推到actor但是还没有执行的任务的数量
  /// Number of tasks that have been pushed to the actor but not executed.
  std::atomic<int64_t> task_queue_length_;

  /// 已经执行的任务数量
  /// Number of executed tasks.
  std::atomic<int64_t> num_executed_tasks_;

  /// 处理任务的事件循环。 
  /// Event loop where tasks are processed.
  instrumented_io_context task_execution_service_;

  /// 保持task_execution_service_ 存活的asio工作
  /// The asio work to keep task_execution_service_ alive.
  boost::asio::io_service::work task_execution_service_work_;

  /// Profiler,包括将分析事件推到GCS上的背景线程。 
  /// Profiler including a background thread that pushes profiling events to the GCS.
  std::shared_ptr<worker::Profiler> profiler_;

  /// 从资源名称到目前为这个woker保留的资源id的图。
  /// 每一对由资源ID和分配给这个worker的资源的一部分组成。这是在任务分配上设置的。 
  /// A map from resource name to the resource IDs that are currently reserved
  /// for this worker. Each pair consists of the resource ID and the fraction
  /// of that resource allocated for this worker. This is set on task assignment.
  std::shared_ptr<ResourceMappingType> resource_ids_ GUARDED_BY(mutex_);

  /// 所有worker模块的普通rpc服务
  /// Common rpc service for all worker modules.
  rpc::CoreWorkerGrpcService grpc_service_;

  /// 用于在排队的actor任务的参数准备就绪时,通知任务接收器。 
  /// Used to notify the task receiver when the arguments of a queued
  /// actor task are ready.
  std::shared_ptr<DependencyWaiterImpl> task_argument_waiter_;

  // 接收直接actor调用的任务的接口。 
  // Interface that receives tasks from direct actor calls.
  std::unique_ptr<CoreWorkerDirectTaskReceiver> direct_task_receiver_;

  // 当指定时间通过时,需要重新提交的任务队列。 
  // Queue of tasks to resubmit when the specified time passes.
  std::deque<std::pair<int64_t, TaskSpecification>> to_resubmit_ GUARDED_BY(mutex_);

  /// 命名了的actor的登记图。
  /// 它不需要持有锁,因为本地模式是单线程的。 
  /// Map of named actor registry. It doesn't need to hold a lock because
  /// local mode is single-threaded.
  absl::flat_hash_map<std::string, ActorID> local_mode_named_actor_registry_;

  // 用于保护'async_ranma_callbacks_'图。
  // Guard for `async_plasma_callbacks_` map.
  mutable absl::Mutex plasma_mutex_;

  // 当plasma对象准备好了时使用的回调
  // Callbacks for when when a plasma object becomes ready.
  absl::flat_hash_map<ObjectID, std::vector<std::function<void(void)>>>
      async_plasma_callbacks_ GUARDED_BY(plasma_mutex_);

  // 当GetAsync不能直接获取请求对象时,要返回回退。 
  // Fallback for when GetAsync cannot directly get the requested object.
  void PlasmaCallback(SetResultCallback success, std::shared_ptr<RayObject> ray_object,
                      ObjectID object_id, void *py_future);

  /// 我们是否关闭并且不再运行更多任务。
  /// Whether we are shutting down and not running further tasks.
  bool exiting_ = false;

  int64_t max_direct_call_object_size_;

  friend class CoreWorkerTest;

  std::unique_ptr<rpc::JobConfig> job_config_;
};

}  // namespace core
}  // namespace ray
