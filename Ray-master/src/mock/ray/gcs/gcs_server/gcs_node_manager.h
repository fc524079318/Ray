// Copyright  The Ray Authors.
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

namespace ray {
namespace gcs {

class MockGcsNodeManager : public GcsNodeManager {
 public:
  MOCK_METHOD(void, HandleRegisterNode,
              (const rpc::RegisterNodeRequest &request, rpc::RegisterNodeReply *reply,
               rpc::SendReplyCallback send_reply_callback),
              (override));
  MOCK_METHOD(void, HandleUnregisterNode,
              (const rpc::UnregisterNodeRequest &request, rpc::UnregisterNodeReply *reply,
               rpc::SendReplyCallback send_reply_callback),
              (override));
  MOCK_METHOD(void, HandleGetAllNodeInfo,
              (const rpc::GetAllNodeInfoRequest &request, rpc::GetAllNodeInfoReply *reply,
               rpc::SendReplyCallback send_reply_callback),
              (override));
  MOCK_METHOD(void, HandleGetInternalConfig,
              (const rpc::GetInternalConfigRequest &request,
               rpc::GetInternalConfigReply *reply,
               rpc::SendReplyCallback send_reply_callback),
              (override));
};

}  // namespace gcs
}  // namespace ray
