#include "src/stirling/source_connectors/socket_tracer/testing/protocol_checkers.h"

#include "src/stirling/source_connectors/socket_tracer/http_table.h"
#include "src/stirling/testing/common.h"

namespace px {
namespace stirling {
namespace testing {

namespace http = protocols::http;

//-----------------------------------------------------------------------------
// HTTP Checkers
//-----------------------------------------------------------------------------

std::vector<http::Record> ToRecordVector(const types::ColumnWrapperRecordBatch& rb,
                                         const std::vector<size_t>& indices) {
  std::vector<http::Record> result;

  for (const auto& idx : indices) {
    http::Record r;
    r.req.req_path = rb[kHTTPReqPathIdx]->Get<types::StringValue>(idx);
    r.req.req_method = rb[kHTTPReqMethodIdx]->Get<types::StringValue>(idx);
    r.req.body = rb[kHTTPReqBodyIdx]->Get<types::StringValue>(idx);

    r.resp.resp_status = rb[kHTTPRespStatusIdx]->Get<types::Int64Value>(idx).val;
    r.resp.resp_message = rb[kHTTPRespMessageIdx]->Get<types::StringValue>(idx);
    r.resp.body = rb[kHTTPRespBodyIdx]->Get<types::StringValue>(idx);

    result.push_back(r);
  }
  return result;
}

std::vector<http::Record> GetTargetRecords(const types::ColumnWrapperRecordBatch& record_batch,
                                           int32_t pid) {
  std::vector<size_t> target_record_indices =
      FindRecordIdxMatchesPID(record_batch, kHTTPUPIDIdx, pid);
  return ToRecordVector(record_batch, target_record_indices);
}

}  // namespace testing
}  // namespace stirling
}  // namespace px
