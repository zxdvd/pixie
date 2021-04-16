#pragma once

#include <google/protobuf/descriptor.pb.h>

#include "src/stirling/source_connectors/socket_tracer/protocols/http2/testing/proto/greet.pb.h"

namespace px {
namespace stirling {
namespace protocols {
namespace http2 {
namespace testing {

inline google::protobuf::FileDescriptorSet GreetServiceFDSet() {
  google::protobuf::FileDescriptorSet res;
  HelloReply::descriptor()->file()->CopyTo(res.add_file());
  return res;
}

}  // namespace testing
}  // namespace http2
}  // namespace protocols
}  // namespace stirling
}  // namespace px
