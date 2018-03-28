// Copyright 2016 Cloudera Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "hs2client/service.h"

#include <sstream>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>
#include <thrift/stdcxx.h>

#include "hs2client/session.h"
#include "hs2client/logging.h"
#include "hs2client/thrift-internal.h"

#include "gen-cpp/ImpalaHiveServer2Service.h"
#include "gen-cpp/TCLIService.h"

namespace hs2 = apache::hive::service::cli::thrift;

using apache::thrift::TException;
using apache::thrift::protocol::TBinaryProtocol;
using apache::thrift::protocol::TProtocol;
using apache::thrift::transport::TBufferedTransport;
using apache::thrift::transport::TSocket;
using apache::thrift::transport::TTransport;
using apache::thrift::stdcxx::shared_ptr;
using std::string;
using std::unique_ptr;

namespace hs2client {

struct Service::ServiceImpl {
  hs2::TProtocolVersion::type protocol_version;
  // Using apache::thrift::stdcxx::shared_ptr to support thrift built with std::shared_ptr and boost::shared_ptr too
  shared_ptr<TSocket> socket;
  shared_ptr<TTransport> transport;
  shared_ptr<TProtocol> protocol;
};

Status Service::Connect(const string& host, int port, int conn_timeout,
    ProtocolVersion protocol_version, unique_ptr<Service>* service) {
  service->reset(new Service(host, port, conn_timeout, protocol_version));
  return (*service)->Open();
}

Service::~Service() {
  DCHECK(!IsConnected());
}

Status Service::Close() {
  if (!IsConnected()) return Status::OK();
  TRY_RPC_OR_RETURN(impl_->transport->close());
  return Status::OK();
}

bool Service::IsConnected() const {
  return impl_->transport && impl_->transport->isOpen();
}

void Service::SetRecvTimeout(int timeout) {
  impl_->socket->setRecvTimeout(timeout);
}

void Service::SetSendTimeout(int timeout) {
  impl_->socket->setSendTimeout(timeout);
}

Status Service::OpenSession(const string& user, const HS2ClientConfig& config,
    unique_ptr<Session>* session) const {
  session->reset(new Session(rpc_));
  return (*session)->Open(config, user);
}

Service::Service(const string& host, int port, int conn_timeout,
    ProtocolVersion protocol_version)
  : host_(host), port_(port), conn_timeout_(conn_timeout), impl_(new ServiceImpl()),
    rpc_(new ThriftRPC()) {
  impl_->protocol_version = ProtocolVersionToTProtocolVersion(protocol_version);
}

Status Service::Open() {
  if (impl_->protocol_version < hs2::TProtocolVersion::HIVE_CLI_SERVICE_PROTOCOL_V6) {
    std::stringstream ss;
    ss << "Unsupported protocol: " << impl_->protocol_version;
    return Status::Error(ss.str());
  }

  impl_->socket.reset(new TSocket(host_, port_));
  impl_->socket->setConnTimeout(conn_timeout_);
  impl_->transport.reset(new TBufferedTransport(impl_->socket));
  impl_->protocol.reset(new TBinaryProtocol(impl_->transport));

  rpc_->client.reset(new impala::ImpalaHiveServer2ServiceClient(impl_->protocol));

  TRY_RPC_OR_RETURN(impl_->transport->open());

  return Status::OK();
}

} // namespace hs2client
