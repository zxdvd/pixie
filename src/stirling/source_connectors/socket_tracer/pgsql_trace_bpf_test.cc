#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <absl/strings/str_split.h>

#include <string>
#include <string_view>

#include "src/common/exec/exec.h"
#include "src/common/testing/test_utils/container_runner.h"
#include "src/stirling/core/output.h"
#include "src/stirling/source_connectors/socket_tracer/testing/socket_trace_bpf_test_fixture.h"
#include "src/stirling/testing/common.h"

namespace px {
namespace stirling {

using ::px::stirling::testing::AccessRecordBatch;
using ::px::stirling::testing::FindRecordIdxMatchesPID;
using ::px::testing::BazelBinTestFilePath;
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Pair;
using ::testing::SizeIs;
using ::testing::StrEq;

class PostgreSQLContainer : public ContainerRunner {
 public:
  PostgreSQLContainer()
      : ContainerRunner(BazelBinTestFilePath(kBazelImageTar), kContainerNamePrefix, kReadyMessage) {
  }

 private:
  static constexpr std::string_view kBazelImageTar =
      "src/stirling/source_connectors/socket_tracer/testing/containers/"
      "postgres_image.tar";
  static constexpr std::string_view kContainerNamePrefix = "postgres_testing";
  static constexpr std::string_view kReadyMessage =
      "database system is ready to accept connections";
};

class GolangSQLxContainer : public ContainerRunner {
 public:
  GolangSQLxContainer()
      : ContainerRunner(BazelBinTestFilePath(kBazelImageTar), kContainerNamePrefix, kReadyMessage) {
  }

 private:
  static constexpr std::string_view kBazelImageTar =
      "src/stirling/source_connectors/socket_tracer/testing/containers/pgsql/demo_client_image.tar";
  static constexpr std::string_view kContainerNamePrefix = "pgsql_demo";
  static constexpr std::string_view kReadyMessage = "";
};

class PostgreSQLTraceTest : public testing::SocketTraceBPFTest</* TClientSideTracing */ true> {
 protected:
  PostgreSQLTraceTest() { PL_CHECK_OK(container_.Run(150, {"--env=POSTGRES_PASSWORD=docker"})); }

  PostgreSQLContainer container_;
};

// TODO(yzhao): We want to test Stirling's behavior when intercept the middle of the traffic.
// One way is to let SubProcess able to accept STDIN after launching. This test does not have that
// capability because it's running a query from start to finish, which always establish new
// connections.
TEST_F(PostgreSQLTraceTest, SelectQuery) {
  StartTransferDataThread(SocketTraceConnector::kPGSQLTableNum, kPGSQLTable);

  // --pid host is required to access the correct PID.
  constexpr char kCmdTmpl[] =
      "docker run --pid host --rm -e PGPASSWORD=docker --network=container:$0 postgres bash -c "
      R"('psql -h localhost -U postgres -c "$1" &>/dev/null & echo $$! && wait')";
  const std::string kCreateTableCmd =
      absl::Substitute(kCmdTmpl, container_.container_name(),
                       "create table foo (field0 serial primary key);"
                       "insert into foo values (12345);"
                       "select * from foo;");
  ASSERT_OK_AND_ASSIGN(const std::string create_table_output, px::Exec(kCreateTableCmd));
  int32_t client_pid;
  ASSERT_TRUE(absl::SimpleAtoi(create_table_output, &client_pid));

  std::vector<TaggedRecordBatch> tablets = StopTransferDataThread();
  ASSERT_FALSE(tablets.empty());
  types::ColumnWrapperRecordBatch record_batch = tablets[0].records;
  auto indices = FindRecordIdxMatchesPID(record_batch, kPGSQLUPIDIdx, client_pid);
  ASSERT_THAT(indices, SizeIs(1));

  EXPECT_THAT(AccessRecordBatch<types::StringValue>(record_batch, kPGSQLReqIdx, indices[0]),
              HasSubstr("create table foo (field0 serial primary key);"
                        "insert into foo values (12345);"
                        "select * from foo;"));
  EXPECT_THAT(AccessRecordBatch<types::StringValue>(record_batch, kPGSQLRespIdx, indices[0]),
              // TODO(PP-1920): This is a bug, it should return output for the other 2 queries.
              StrEq("CREATE TABLE"));
}

class PostgreSQLTraceGoSQLxTest
    : public testing::SocketTraceBPFTest</* TClientSideTracing */ true> {
 protected:
  PostgreSQLTraceGoSQLxTest() {
    PL_CHECK_OK(pgsql_container_.Run(150, {"--env=POSTGRES_PASSWORD=docker"}));
  }

  PostgreSQLContainer pgsql_container_;
  GolangSQLxContainer sqlx_container_;
};

std::vector<std::pair<std::string, std::string>> RecordBatchToPairs(
    const types::ColumnWrapperRecordBatch& record_batch, const std::vector<size_t>& indices) {
  std::vector<std::pair<std::string, std::string>> res;
  for (size_t i : indices) {
    res.push_back({AccessRecordBatch<types::StringValue>(record_batch, kPGSQLReqIdx, i),
                   AccessRecordBatch<types::StringValue>(record_batch, kPGSQLRespIdx, i)});
  }
  return res;
}

// Executes a demo golang app that queries PostgreSQL database with sqlx.
TEST_F(PostgreSQLTraceGoSQLxTest, GolangSqlxDemo) {
  StartTransferDataThread(SocketTraceConnector::kPGSQLTableNum, kPGSQLTable);

  PL_CHECK_OK(sqlx_container_.Run(
      10, {absl::Substitute("--network=container:$0", pgsql_container_.container_name())}));

  std::vector<TaggedRecordBatch> tablets = StopTransferDataThread();
  ASSERT_FALSE(tablets.empty());
  types::ColumnWrapperRecordBatch record_batch = tablets[0].records;

  // Select only the records from the client side. Stirling captures both client and server side
  // traffic because of the remote address is outside of the cluster.
  const auto indices =
      FindRecordIdxMatchesPID(record_batch, kPGSQLUPIDIdx, sqlx_container_.process_pid());

  EXPECT_THAT(
      RecordBatchToPairs(record_batch, indices),
      ElementsAre(
          Pair("QUERY [CREATE TABLE IF NOT EXISTS person (\n"
               "    first_name text,\n"
               "    last_name text,\n"
               "    email text\n)]",
               "CREATE TABLE"),
          Pair("QUERY [BEGIN READ WRITE]", "BEGIN"),
          Pair("PARSE [INSERT INTO person (first_name, last_name, email) VALUES ($1, $2, $3)]",
               "PARSE COMPLETE"),
          Pair("DESCRIBE [type=kStatement name=]", "ROW DESCRIPTION "),
          Pair("BIND [portal= statement= parameters=[[formt=kText value=Jason], "
               "[formt=kText value=Moiron], "
               "[formt=kText value=jmoiron@jmoiron.net]] result_format_codes=[]]",
               "BIND COMPLETE"),
          Pair("EXECUTE [query=[INSERT INTO person (first_name, last_name, email) VALUES "
               "($1, $2, $3)], params=[Jason, Moiron, jmoiron@jmoiron.net]]",
               "INSERT 0 1"),
          Pair("QUERY [COMMIT]", "COMMIT"),
          Pair("PARSE [SELECT * FROM person WHERE first_name=$1]", "PARSE COMPLETE"),
          Pair("DESCRIBE [type=kStatement name=]",
               "ROW DESCRIPTION [name=first_name table_oid=16384 attr_num=1 type_oid=25 "
               "type_size=-1 type_modifier=-1 fmt_code=kText] "
               "[name=last_name table_oid=16384 attr_num=2 type_oid=25 type_size=-1 "
               "type_modifier=-1 fmt_code=kText] "
               "[name=email table_oid=16384 attr_num=3 type_oid=25 type_size=-1 "
               "type_modifier=-1 fmt_code=kText]"),
          Pair("BIND [portal= statement= parameters=[[formt=kText value=Jason]] "
               "result_format_codes=[]]",
               "BIND COMPLETE"),
          Pair("EXECUTE [query=[SELECT * FROM person WHERE first_name=$1], params=[Jason]]",
               "Jason,Moiron,jmoiron@jmoiron.net\n"
               "SELECT 1")));
}

TEST_F(PostgreSQLTraceTest, FunctionCall) {
  // --pid host is required to access the correct PID.
  constexpr char kCmdTmpl[] =
      "docker run --pid host --rm -e PGPASSWORD=docker --network=container:$0 postgres bash -c "
      R"('psql -h localhost -U postgres -c "$1" &>/dev/null & echo $$! && wait')";
  {
    StartTransferDataThread(SocketTraceConnector::kPGSQLTableNum, kPGSQLTable);

    const std::string cmd = absl::Substitute(
        kCmdTmpl, container_.container_name(),
        "CREATE OR REPLACE FUNCTION increment(i integer) RETURNS integer AS \\$\\$\n"
        "BEGIN\n"
        "      RETURN i + 1;\n"
        "END;\n"
        "\\$\\$ LANGUAGE plpgsql;");
    ASSERT_OK_AND_ASSIGN(const std::string output, px::Exec(cmd));
    int32_t client_pid;
    ASSERT_TRUE(absl::SimpleAtoi(output, &client_pid));

    std::vector<TaggedRecordBatch> tablets = StopTransferDataThread();
    ASSERT_FALSE(tablets.empty());
    types::ColumnWrapperRecordBatch record_batch = tablets[0].records;

    auto indices = FindRecordIdxMatchesPID(record_batch, kPGSQLUPIDIdx, client_pid);
    ASSERT_THAT(indices, SizeIs(1));

    EXPECT_THAT(
        std::string(AccessRecordBatch<types::StringValue>(record_batch, kPGSQLReqIdx, indices[0])),
        StrEq("QUERY [CREATE OR REPLACE FUNCTION increment(i integer) RETURNS integer AS $$\n"
              "BEGIN\n"
              "      RETURN i + 1;\n"
              "END;\n"
              "$$ LANGUAGE plpgsql;]"));
    EXPECT_THAT(
        std::string(AccessRecordBatch<types::StringValue>(record_batch, kPGSQLRespIdx, indices[0])),
        StrEq("CREATE FUNCTION"));
  }
  {
    StartTransferDataThread(SocketTraceConnector::kPGSQLTableNum, kPGSQLTable);

    const std::string cmd =
        absl::Substitute(kCmdTmpl, container_.container_name(), "select increment(1);");
    ASSERT_OK_AND_ASSIGN(const std::string output, px::Exec(cmd));
    int32_t client_pid;
    ASSERT_TRUE(absl::SimpleAtoi(output, &client_pid));

    std::vector<TaggedRecordBatch> tablets = StopTransferDataThread();
    ASSERT_FALSE(tablets.empty());
    types::ColumnWrapperRecordBatch record_batch = tablets[0].records;

    auto indices = FindRecordIdxMatchesPID(record_batch, kPGSQLUPIDIdx, client_pid);
    ASSERT_THAT(indices, SizeIs(1));

    EXPECT_THAT(
        std::string(AccessRecordBatch<types::StringValue>(record_batch, kPGSQLReqIdx, indices[0])),
        StrEq("QUERY [select increment(1);]"));
    EXPECT_THAT(
        std::string(AccessRecordBatch<types::StringValue>(record_batch, kPGSQLRespIdx, indices[0])),
        StrEq("increment\n"
              "2\n"
              "SELECT 1"));
  }
}

}  // namespace stirling
}  // namespace px
