#pragma once

#include <string>
#include <string_view>

#include "src/common/base/base.h"
#include "src/shared/types/column_wrapper.h"
#include "src/stirling/core/types.h"

namespace px {
namespace stirling {

// Returns a string representation of the row specified by index.
std::string ToString(const stirlingpb::TableSchema& schema,
                     const types::ColumnWrapperRecordBatch& record_batch, size_t index);

std::string ToString(std::string_view prefix, const stirlingpb::TableSchema& schema,
                     const types::ColumnWrapperRecordBatch& record_batch);

void PrintRecordBatch(std::string_view prefix, const stirlingpb::TableSchema& schema,
                      const types::ColumnWrapperRecordBatch& record_batch);

}  // namespace stirling
}  // namespace px
