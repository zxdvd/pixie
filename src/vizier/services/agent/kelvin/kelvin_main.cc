#include <grpcpp/grpcpp.h>
#include <algorithm>

#include <sole.hpp>

#include "src/vizier/services/agent/kelvin/kelvin_manager.h"

#include "src/common/base/base.h"
#include "src/common/event/nats.h"
#include "src/common/signal/signal.h"
#include "src/shared/version/version.h"

DEFINE_string(nats_url, gflags::StringFromEnv("PL_NATS_URL", "pl-nats"),
              "The host address of the nats cluster");

DEFINE_string(mds_addr, gflags::StringFromEnv("PL_MDS_ADDR", "vizier-metadata.pl.svc:50400"),
              "The host address of the MDS");

DEFINE_string(pod_ip, gflags::StringFromEnv("PL_POD_IP", ""),
              "The IP address of the pod this controller is running on");

DEFINE_int32(rpc_port, gflags::Int32FromEnv("PL_RPC_PORT", 59300), "The port of the RPC server");

DEFINE_string(pod_name, gflags::StringFromEnv("PL_POD_NAME", ""),
              "The name of the POD the PEM is running on");

DEFINE_string(host_ip, gflags::StringFromEnv("PL_HOST_IP", ""),
              "The IP of the host this service is running on");

using ::px::vizier::agent::KelvinManager;
using ::px::vizier::agent::Manager;

class AgentDeathHandler : public px::FatalErrorHandlerInterface {
 public:
  AgentDeathHandler() = default;
  void OnFatalError() const override {
    // Stack trace will print automatically; any additional state dumps can be done here.
    // Note that actions here must be async-signal-safe and must not allocate memory.
  }
};

// Signal handlers for graceful termination.
class AgentTerminationHandler {
 public:
  // This list covers signals that are handled gracefully.
  static constexpr auto kSignals = ::px::MakeArray(SIGINT, SIGQUIT, SIGTERM, SIGHUP);

  static void InstallSignalHandlers() {
    for (size_t i = 0; i < kSignals.size(); ++i) {
      signal(kSignals[i], AgentTerminationHandler::OnTerminate);
    }
  }

  static void set_manager(Manager* manager) { manager_ = manager; }

  static void OnTerminate(int signum) {
    if (manager_ != nullptr) {
      LOG(INFO) << "Trying to gracefully stop agent manager";
      auto s = manager_->Stop(std::chrono::seconds{5});
      if (!s.ok()) {
        LOG(ERROR) << "Failed to gracefully stop agent manager, it will terminate shortly.";
      }
      exit(signum);
    }
  }

 private:
  inline static Manager* manager_ = nullptr;
};

int main(int argc, char** argv) {
  px::EnvironmentGuard env_guard(&argc, argv);

  AgentDeathHandler err_handler;

  // This covers signals such as SIGSEGV and other fatal errors.
  // We print the stack trace and die.
  auto signal_action = std::make_unique<px::SignalAction>();
  signal_action->RegisterFatalErrorHandler(err_handler);

  // Install signal handlers where graceful exit is possible.
  AgentTerminationHandler::InstallSignalHandlers();

  sole::uuid agent_id = sole::uuid4();
  LOG(INFO) << absl::Substitute("Pixie Kelvin. Version: $0, id: $1",
                                px::VersionInfo::VersionString(), agent_id.str());
  if (FLAGS_pod_ip.length() == 0) {
    LOG(FATAL) << "The POD_IP must be specified";
  }
  if (FLAGS_host_ip.length() == 0) {
    LOG(FATAL) << "The HOST_IP must be specified";
  }

  std::string addr = absl::Substitute("$0:$1", FLAGS_pod_ip, FLAGS_rpc_port);

  auto manager = KelvinManager::Create(agent_id, FLAGS_pod_name, FLAGS_host_ip, addr,
                                       FLAGS_rpc_port, FLAGS_nats_url, FLAGS_mds_addr)
                     .ConsumeValueOrDie();

  AgentTerminationHandler::set_manager(manager.get());

  PL_CHECK_OK(manager->Run());
  PL_CHECK_OK(manager->Stop(std::chrono::seconds{1}));

  // Clear the manager, because it has been stopped.
  AgentTerminationHandler::set_manager(nullptr);

  return 0;
}
