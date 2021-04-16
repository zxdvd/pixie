#pragma once

#include <map>

#include "src/stirling/core/types.h"
#include "src/stirling/source_connectors/socket_tracer/canonical_types.h"
#include "src/stirling/source_connectors/socket_tracer/protocols/redis/types.h"

namespace px {
namespace stirling {

// clang-format off
static constexpr DataElement kRedisElements[] = {
        canonical_data_elements::kTime,
        canonical_data_elements::kUPID,
        canonical_data_elements::kRemoteAddr,
        canonical_data_elements::kRemotePort,
        canonical_data_elements::kTraceRole,
        {"req_cmd", "Request command. See https://redis.io/commands.",
         types::DataType::STRING,
         types::SemanticType::ST_NONE,
         types::PatternType::GENERAL},
        {"req_args", "Request command arguments.",
         types::DataType::STRING,
         types::SemanticType::ST_NONE,
         types::PatternType::GENERAL},
        {"resp", "Response message.",
         types::DataType::STRING,
         types::SemanticType::ST_NONE,
         types::PatternType::GENERAL},
        canonical_data_elements::kLatencyNS,
#ifndef NDEBUG
        {"px_info_", "Pixie messages regarding the record (e.g. warnings)",
         types::DataType::STRING,
         types::SemanticType::ST_NONE,
         types::PatternType::GENERAL},
#endif
};
// clang-format on

static constexpr auto kRedisTable =
    DataTableSchema("redis_events", "Redis request-response pair events", kRedisElements,
                    std::chrono::milliseconds{100}, std::chrono::milliseconds{1000});

constexpr int kRedisUPIDIdx = kRedisTable.ColIndex("upid");
constexpr int kRedisCmdIdx = kRedisTable.ColIndex("req_cmd");
constexpr int kRedisReqIdx = kRedisTable.ColIndex("req_args");
constexpr int kRedisRespIdx = kRedisTable.ColIndex("resp");

}  // namespace stirling
}  // namespace px
