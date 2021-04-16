#pragma once

#include <chrono>
#include <string>

#include "src/common/base/utils.h"
#include "src/stirling/source_connectors/socket_tracer/protocols/common/event_parser.h"  // For FrameBase

namespace px {
namespace stirling {
namespace protocols {
namespace http {

// Automatically converts ToString() to stream operator for gtest.
using ::px::operator<<;

//-----------------------------------------------------------------------------
// HTTP Message
//-----------------------------------------------------------------------------

// HTTP1.x headers can have multiple values for the same name, and field names are case-insensitive:
// https://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.2
using HeadersMap = std::multimap<std::string, std::string, CaseInsensitiveLess>;

inline constexpr char kContentEncoding[] = "Content-Encoding";
inline constexpr char kContentLength[] = "Content-Length";
inline constexpr char kContentType[] = "Content-Type";
inline constexpr char kTransferEncoding[] = "Transfer-Encoding";
inline constexpr char kUpgrade[] = "Upgrade";

struct Message : public FrameBase {
  MessageType type = MessageType::kUnknown;

  int minor_version = -1;
  HeadersMap headers = {};

  std::string req_method = "-";
  std::string req_path = "-";

  int resp_status = -1;
  std::string resp_message = "-";

  std::string body = "-";

  // The number of bytes in the HTTP header, used in ByteSize(),
  // as an approximation of the size of the non-body fields.
  size_t headers_byte_size = 0;

  size_t ByteSize() const override {
    return sizeof(Message) + headers_byte_size + body.size() + resp_message.size();
  }

  std::string ToString() const {
    return absl::Substitute(
        "[type=$0 minor_version=$1 headers=[$2] req_method=$3 "
        "req_path=$4 resp_status=$5 resp_message=$6 body=$7]",
        magic_enum::enum_name(type), minor_version,
        absl::StrJoin(headers, ",", absl::PairFormatter(":")), req_method, req_path, resp_status,
        resp_message, body);
  }
};

//-----------------------------------------------------------------------------
// Table Store Entry Level Structs
//-----------------------------------------------------------------------------

/**
 *  Record is the primary output of the http stitcher.
 */
struct Record {
  Message req;
  Message resp;

  // Debug information that we want to pass up this record.
  // Used to record info/warnings.
  // Only pushed to table store on debug builds.
  std::string px_info = "";

  std::string ToString() const {
    return absl::Substitute("[req=$0 resp=$1]", req.ToString(), resp.ToString());
  }
};

struct ProtocolTraits {
  using frame_type = Message;
  using record_type = Record;
  using state_type = NoState;
};

}  // namespace http
}  // namespace protocols
}  // namespace stirling
}  // namespace px
