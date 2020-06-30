#include "src/stirling/dynamic_tracing/code_gen.h"

#include <utility>

#include <absl/container/flat_hash_set.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/substitute.h>

#include "src/common/base/base.h"

namespace pl {
namespace stirling {
namespace dynamic_tracing {

using ::pl::stirling::bpf_tools::BPFProbeAttachType;
using ::pl::stirling::bpf_tools::UProbeSpec;
using ::pl::stirling::dynamic_tracing::ir::physical::PhysicalProbe;
using ::pl::stirling::dynamic_tracing::ir::physical::Program;
using ::pl::stirling::dynamic_tracing::ir::physical::Register;
using ::pl::stirling::dynamic_tracing::ir::physical::ScalarVariable;
using ::pl::stirling::dynamic_tracing::ir::physical::Struct;
using ::pl::stirling::dynamic_tracing::ir::physical::StructVariable;
using ::pl::stirling::dynamic_tracing::ir::shared::BPFHelper;
using ::pl::stirling::dynamic_tracing::ir::shared::Map;
using ::pl::stirling::dynamic_tracing::ir::shared::MapStashAction;
using ::pl::stirling::dynamic_tracing::ir::shared::Output;
using ::pl::stirling::dynamic_tracing::ir::shared::OutputAction;
using ::pl::stirling::dynamic_tracing::ir::shared::ScalarType;
using ::pl::stirling::dynamic_tracing::ir::shared::TracePoint;
using ::pl::stirling::dynamic_tracing::ir::shared::VariableType;

#define PB_ENUM_SENTINEL_SWITCH_CLAUSE                             \
  LOG(DFATAL) << "Cannot happen. Needed to avoid default clause."; \
  break

#define GCC_SWITCH_RETURN                                \
  LOG(DFATAL) << "Cannot happen. Needed for GCC build."; \
  return {}

namespace {

// clang-format off
const absl::flat_hash_map<ScalarType, std::string_view> kScalarTypeToCType = {
    {ScalarType::BOOL, "bool"},
    {ScalarType::INT, "int"},
    {ScalarType::INT8, "int8_t"},
    {ScalarType::INT16, "int16_t"},
    {ScalarType::INT32, "int32_t"},
    {ScalarType::INT64, "int64_t"},
    {ScalarType::UINT, "uint"},
    {ScalarType::UINT8, "uint8_t"},
    {ScalarType::UINT16, "uint16_t"},
    {ScalarType::UINT32, "uint32_t"},
    {ScalarType::UINT64, "uint64_t"},
    {ScalarType::FLOAT, "float"},
    {ScalarType::DOUBLE, "double"},
    {ScalarType::STRING, "char*"},
    {ScalarType::VOID_POINTER, "void*"},
};
// clang-format on

std::string_view GenScalarType(ScalarType type) {
  auto iter = kScalarTypeToCType.find(type);
  if (iter == kScalarTypeToCType.end()) {
    LOG(DFATAL) << absl::Substitute("Mapping to C-type not present for $0",
                                    magic_enum::enum_name(type));
    // Should never get here, but return "int" just in case.
    return "int";
  }
  return iter->second;
}

StatusOr<std::string> GenVariableType(const VariableType& var_type) {
  switch (var_type.type_oneof_case()) {
    case VariableType::TypeOneofCase::kScalar:
      return std::string(GenScalarType(var_type.scalar()));
    case VariableType::TypeOneofCase::kStructType:
      return absl::Substitute("struct $0", var_type.struct_type());
    case VariableType::TypeOneofCase::TYPE_ONEOF_NOT_SET:
      return error::InvalidArgument("Field type must be set");
  }
  GCC_SWITCH_RETURN;
}

StatusOr<std::string> GenField(const Struct::Field& field) {
  PL_ASSIGN_OR_RETURN(std::string type_code, GenVariableType(field.type()));
  return absl::Substitute("$0 $1;", type_code, field.name());
}

}  // namespace

StatusOr<std::vector<std::string>> GenStruct(const Struct& st, int member_indent_size) {
  DCHECK_GT(st.fields_size(), 0);

  std::vector<std::string> code_lines;

  code_lines.push_back(absl::Substitute("struct $0 {", st.name()));

  for (const auto& field : st.fields()) {
    PL_ASSIGN_OR_RETURN(std::string field_code, GenField(field));

    code_lines.push_back(absl::StrCat(std::string(member_indent_size, ' '), field_code));
  }

  code_lines.push_back("};");

  return code_lines;
}

namespace {

std::vector<std::string> GenRegister(const ScalarVariable& var) {
  switch (var.reg()) {
    case Register::SP:
      return {absl::Substitute("$0 $1 = PT_REGS_SP(ctx);", GenScalarType(var.type()), var.name())};
    case Register::Register_INT_MIN_SENTINEL_DO_NOT_USE_:
    case Register::Register_INT_MAX_SENTINEL_DO_NOT_USE_:
      PB_ENUM_SENTINEL_SWITCH_CLAUSE;
  }
  GCC_SWITCH_RETURN;
}

std::vector<std::string> GenMemoryVariable(const ScalarVariable& var) {
  std::vector<std::string> code_lines;
  code_lines.push_back(absl::Substitute("$0 $1;", GenScalarType(var.type()), var.name()));
  code_lines.push_back(absl::Substitute("bpf_probe_read(&$0, sizeof($1), $2 + $3);", var.name(),
                                        GenScalarType(var.type()), var.memory().base(),
                                        var.memory().offset()));
  return code_lines;
}

std::vector<std::string> GenBPFHelper(const ScalarVariable& var) {
  static const absl::flat_hash_map<BPFHelper, std::string_view> kBPFHelpers = {
      {BPFHelper::GOID, "goid()"},
      {BPFHelper::TGID, "bpf_get_current_pid_tgid() >> 32"},
      {BPFHelper::TGID_PID, "bpf_get_current_pid_tgid()"},
      {BPFHelper::KTIME, "bpf_ktime_get_ns()"},
  };
  auto iter = kBPFHelpers.find(var.builtin());
  DCHECK(iter != kBPFHelpers.end());
  return {absl::Substitute("$0 $1 = $2;", GenScalarType(var.type()), var.name(), iter->second)};
}

}  // namespace

StatusOr<std::vector<std::string>> GenScalarVariable(const ScalarVariable& var) {
  switch (var.address_oneof_case()) {
    case ScalarVariable::AddressOneofCase::kReg:
      return GenRegister(var);
    case ScalarVariable::AddressOneofCase::kMemory:
      return GenMemoryVariable(var);
    case ScalarVariable::AddressOneofCase::kBuiltin:
      return GenBPFHelper(var);
    case ScalarVariable::AddressOneofCase::ADDRESS_ONEOF_NOT_SET:
      return error::InvalidArgument("address_oneof must be set");
  }
  GCC_SWITCH_RETURN;
}

StatusOr<std::vector<std::string>> GenStructVariable(const Struct& st,
                                                     const StructVariable& st_var) {
  if (st.name() != st_var.type()) {
    return error::InvalidArgument("Names of the struct do not match, $0 vs. $1", st.name(),
                                  st_var.type());
  }
  if (st.fields_size() != st_var.variable_names_size()) {
    return error::InvalidArgument(
        "The number of struct fields and variables do not match, $0 vs. $1", st.fields_size(),
        st_var.variable_names_size());
  }

  std::vector<std::string> code_lines;

  code_lines.push_back(absl::Substitute("struct $0 $1 = {};", st.name(), st_var.name()));

  for (int i = 0; i < st.fields_size() && i < st_var.variable_names_size(); ++i) {
    const auto& var_name = st_var.variable_names(i);
    if (var_name.name_oneof_case() != StructVariable::VariableName::NameOneofCase::kName) {
      continue;
    }
    code_lines.push_back(absl::Substitute("$0.$1 = $2;", st_var.name(), st.fields(i).name(),
                                          st_var.variable_names(i).name()));
  }

  return code_lines;
}

// TODO(yzhao): Wrap map stash action inside "{}" to avoid variable naming conflict.
//
// TODO(yzhao): Alternatively, leave map key as another Variable message (would be pre-generated
// as part of the physical IR).
std::vector<std::string> GenMapStashAction(const MapStashAction& action) {
  return {absl::Substitute("$0.update(&$1, &$2);", action.map_name(), action.key_variable_name(),
                           action.value_variable_name())};
}

std::vector<std::string> GenOutput(const Output& output) {
  return {absl::Substitute("BPF_PERF_OUTPUT($0);", output.name())};
}

std::vector<std::string> GenOutputAction(const OutputAction& action) {
  return {absl::Substitute("$0.perf_submit(ctx, &$1, sizeof($1));", action.perf_buffer_name(),
                           action.variable_name())};
}

namespace {

void MoveBackStrVec(std::vector<std::string>&& src, std::vector<std::string>* dst) {
  dst->insert(dst->end(), std::make_move_iterator(src.begin()), std::make_move_iterator(src.end()));
}

}  // namespace

StatusOr<std::vector<std::string>> GenPhysicalProbe(
    const absl::flat_hash_map<std::string_view, const Struct*>& structs,
    const PhysicalProbe& probe) {
  if (probe.name().empty()) {
    return error::InvalidArgument("Probe's name cannot be empty");
  }

  std::vector<std::string> code_lines;

#define MOVE_BACK_STR_VEC(dst, expr)                           \
  PL_ASSIGN_OR_RETURN(std::vector<std::string> str_vec, expr); \
  MoveBackStrVec(std::move(str_vec), dst);

  code_lines.push_back(absl::Substitute("int $0(struct pt_regs* ctx) {", probe.name()));

  absl::flat_hash_set<std::string_view> var_names;

  for (const auto& var : probe.vars()) {
    var_names.insert(var.name());
    MOVE_BACK_STR_VEC(&code_lines, GenScalarVariable(var));
  }

  for (const auto& var : probe.st_vars()) {
    for (const auto& var_name : var.variable_names()) {
      if (var_name.name_oneof_case() != StructVariable::VariableName::NameOneofCase::kName) {
        continue;
      }
      if (!var_names.contains(var_name.name())) {
        return error::InvalidArgument(
            "variable name '$0' assigned to struct variable '$1' was not defined", var_name.name(),
            var.name());
      }
      // TODO(yzhao): Check variable types as well.
    }

    if (var_names.contains(var.name())) {
      return error::InvalidArgument("variable name '$0' was redefined", var.name());
    }

    var_names.insert(var.name());

    auto iter = structs.find(var.type());
    if (iter == structs.end()) {
      return error::InvalidArgument("Struct name '$0' referenced in variable '$1' was not defined",
                                    var.type(), var.name());
    }
    MOVE_BACK_STR_VEC(&code_lines, GenStructVariable(*iter->second, var));
  }

  for (const auto& action : probe.map_stash_actions()) {
    if (!var_names.contains(action.key_variable_name())) {
      return error::InvalidArgument(
          "variable name '$0' as the key pushed to BPF map '$1' was not defined",
          action.key_variable_name(), action.map_name());
    }
    if (!var_names.contains(action.value_variable_name())) {
      return error::InvalidArgument(
          "variable name '$0' as the value pushed to BPF map '$1' was not defined",
          action.value_variable_name(), action.map_name());
    }
    MoveBackStrVec(GenMapStashAction(action), &code_lines);
  }

  for (const auto& action : probe.output_actions()) {
    if (!var_names.contains(action.variable_name())) {
      return error::InvalidArgument(
          "variable name '$0' submitted to perf buffer '$1' was not defined",
          action.variable_name(), action.perf_buffer_name());
    }
    MoveBackStrVec(GenOutputAction(action), &code_lines);
  }

  code_lines.push_back("return 0;");
  code_lines.push_back("}");

  return code_lines;
}

namespace {

UProbeSpec GetUProbeSpec(const PhysicalProbe& probe) {
  UProbeSpec spec;

  spec.binary_path = probe.trace_point().binary_path();
  spec.symbol = probe.trace_point().symbol();
  DCHECK(probe.trace_point().type() == TracePoint::ENTRY ||
         probe.trace_point().type() == TracePoint::RETURN);
  // TODO(yzhao): If the binary is go, needs to use kReturnInsts.
  spec.attach_type = probe.trace_point().type() == TracePoint::ENTRY ? BPFProbeAttachType::kEntry
                                                                     : BPFProbeAttachType::kReturn;
  spec.probe_fn = probe.name();

  return spec;
}

StatusOr<std::vector<std::string>> GenMap(const Map& map) {
  PL_ASSIGN_OR_RETURN(std::string key_code, GenVariableType(map.key_type()));
  PL_ASSIGN_OR_RETURN(std::string value_code, GenVariableType(map.value_type()));
  std::vector<std::string> code_lines = {
      absl::Substitute("BPF_HASH($0, $1, $2);", map.name(), key_code, value_code)};
  return code_lines;
}

StatusOr<std::vector<std::string>> GenProgramCodeLines(const Program& program) {
  std::vector<std::string> code_lines;

  code_lines.push_back("#include <linux/ptrace.h>");

  absl::flat_hash_map<std::string_view, const Struct*> structs;

  for (const auto& st : program.structs()) {
    MOVE_BACK_STR_VEC(&code_lines, GenStruct(st));
    structs[st.name()] = &st;
  }

  for (const auto& map : program.maps()) {
    if (map.key_type().type_oneof_case() == VariableType::TypeOneofCase::kStructType &&
        !structs.contains(map.key_type().struct_type())) {
      return error::InvalidArgument("Struct key type '$0' referenced in map '$1' was not defined",
                                    map.key_type().struct_type(), map.name());
    }
    if (map.value_type().type_oneof_case() == VariableType::TypeOneofCase::kStructType &&
        !structs.contains(map.value_type().struct_type())) {
      return error::InvalidArgument("Struct key type '$0' referenced in map '$1' was not defined",
                                    map.value_type().struct_type(), map.name());
    }
    MOVE_BACK_STR_VEC(&code_lines, GenMap(map));
  }

  for (const auto& output : program.outputs()) {
    if (output.type().type_oneof_case() == VariableType::TypeOneofCase::kStructType &&
        !structs.contains(output.type().struct_type())) {
      return error::InvalidArgument(
          "Struct key type '$0' referenced in output '$1' was not defined",
          output.type().struct_type(), output.name());
    }
    MoveBackStrVec(GenOutput(output), &code_lines);
  }

  for (const auto& probe : program.probes()) {
    MOVE_BACK_STR_VEC(&code_lines, GenPhysicalProbe(structs, probe));
  }

  return code_lines;
}

}  // namespace

StatusOr<BCCProgram> GenProgram(const Program& program) {
  BCCProgram res;

  MOVE_BACK_STR_VEC(&res.code_lines, GenProgramCodeLines(program));

  for (const auto& probe : program.probes()) {
    res.uprobe_specs.push_back(GetUProbeSpec(probe));
  }

  return res;
}

}  // namespace dynamic_tracing
}  // namespace stirling
}  // namespace pl
