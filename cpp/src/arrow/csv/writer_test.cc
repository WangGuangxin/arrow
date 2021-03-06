// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "gtest/gtest.h"

#include <memory>
#include <vector>

#include "arrow/buffer.h"
#include "arrow/csv/writer.h"
#include "arrow/io/memory.h"
#include "arrow/record_batch.h"
#include "arrow/result_internal.h"
#include "arrow/testing/gtest_util.h"
#include "arrow/type.h"
#include "arrow/type_fwd.h"

namespace arrow {
namespace csv {

struct TestParams {
  std::shared_ptr<RecordBatch> record_batch;
  WriteOptions options;
  std::string expected_output;
};

WriteOptions DefaultTestOptions(bool include_header) {
  WriteOptions options;
  options.batch_size = 5;
  options.include_header = include_header;
  return options;
}

std::vector<TestParams> GenerateTestCases() {
  auto abc_schema = schema({
      {field("a", uint64())},
      {field("b\"", utf8())},
      {field("c ", int32())},
  });
  auto empty_batch =
      RecordBatch::Make(abc_schema, /*num_rows=*/0,
                        {
                            ArrayFromJSON(abc_schema->field(0)->type(), "[]"),
                            ArrayFromJSON(abc_schema->field(1)->type(), "[]"),
                            ArrayFromJSON(abc_schema->field(2)->type(), "[]"),
                        });
  auto populated_batch = RecordBatchFromJSON(abc_schema, R"([{"a": 1, "c ": -1},
                                                         { "a": 1, "b\"": "abc\"efg", "c ": 2324},
                                                         { "b\"": "abcd", "c ": 5467},
                                                         { },
                                                         { "a": 546, "b\"": "", "c ": 517 },
                                                         { "a": 124, "b\"": "a\"\"b\"" }])");
  std::string expected_without_header = std::string("1,,-1") + "\n" +     // line 1
                                        +R"(1,"abc""efg",2324)" + "\n" +  // line 2
                                        R"(,"abcd",5467)" + "\n" +        // line 3
                                        R"(,,)" + "\n" +                  // line 4
                                        R"(546,"",517)" + "\n" +          // line 5
                                        R"(124,"a""""b""",)" + "\n";      // line 6
  std::string expected_header = std::string(R"("a","b""","c ")") + "\n";

  return std::vector<TestParams>{
      {empty_batch, DefaultTestOptions(/*header=*/false), ""},
      {empty_batch, DefaultTestOptions(/*header=*/true), expected_header},
      {populated_batch, DefaultTestOptions(/*header=*/false), expected_without_header},
      {populated_batch, DefaultTestOptions(/*header=*/true),
       expected_header + expected_without_header}};
}

class TestWriteCSV : public ::testing::TestWithParam<TestParams> {
 protected:
  template <typename Data>
  Result<std::string> ToCsvString(const Data& data, const WriteOptions& options) {
    std::shared_ptr<io::BufferOutputStream> out;
    ASSIGN_OR_RAISE(out, io::BufferOutputStream::Create());

    RETURN_NOT_OK(WriteCSV(data, options, default_memory_pool(), out.get()));
    ASSIGN_OR_RAISE(std::shared_ptr<Buffer> buffer, out->Finish());
    return std::string(reinterpret_cast<const char*>(buffer->data()), buffer->size());
  }
};

TEST_P(TestWriteCSV, TestWrite) {
  ASSERT_OK_AND_ASSIGN(std::shared_ptr<io::BufferOutputStream> out,
                       io::BufferOutputStream::Create());
  WriteOptions options = GetParam().options;
  std::string csv;
  ASSERT_OK_AND_ASSIGN(csv, ToCsvString(*GetParam().record_batch, options));
  EXPECT_EQ(csv, GetParam().expected_output);

  // Batch size shouldn't matter.
  options.batch_size /= 2;
  ASSERT_OK_AND_ASSIGN(csv, ToCsvString(*GetParam().record_batch, options));
  EXPECT_EQ(csv, GetParam().expected_output);

  // Table and Record batch should work identically.
  ASSERT_OK_AND_ASSIGN(std::shared_ptr<Table> table,
                       Table::FromRecordBatches({GetParam().record_batch}));
  ASSERT_OK_AND_ASSIGN(csv, ToCsvString(*table, options));
  EXPECT_EQ(csv, GetParam().expected_output);
}

INSTANTIATE_TEST_SUITE_P(MultiColumnWriteCSVTest, TestWriteCSV,
                         ::testing::ValuesIn(GenerateTestCases()));

INSTANTIATE_TEST_SUITE_P(
    SingleColumnWriteCSVTest, TestWriteCSV,
    ::testing::Values(TestParams{
        RecordBatchFromJSON(schema({field("int64", int64())}),
                            R"([{ "int64": 9999}, {}, { "int64": -15}])"),
        WriteOptions(),
        R"("int64")"
        "\n9999\n\n-15\n"}));

}  // namespace csv
}  // namespace arrow
