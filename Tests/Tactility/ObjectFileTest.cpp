#include "doctest.h"
#include <Tactility/file/ObjectFile.h>

using tt::file::ObjectFileWriter;
using tt::file::ObjectFileReader;

constexpr const char* TEMP_FILE = "test.tmp";

struct TestStruct {
    uint32_t value;
};

TEST_CASE("Writing and reading multiple records to a file") {
    ObjectFileWriter writer = ObjectFileWriter(TEMP_FILE, sizeof(TestStruct), 1, false);

    TestStruct record_out_1 = { .value = 0xAAAAAAAA };
    TestStruct record_out_2 = { .value = 0xBBBBBBBB };

    CHECK_EQ(writer.open(), true);
    CHECK_EQ(writer.write(&record_out_1), true);
    CHECK_EQ(writer.write(&record_out_2), true);
    writer.close();

    TestStruct record_in;
    ObjectFileReader reader = ObjectFileReader(TEMP_FILE, sizeof(TestStruct));
    CHECK_EQ(reader.open(), true);
    CHECK_EQ(reader.hasNext(), true);
    CHECK_EQ(reader.readNext(&record_in), true);
    CHECK_EQ(reader.hasNext(), true);
    CHECK_EQ(reader.readNext(&record_in), true);
    CHECK_EQ(reader.hasNext(), false);
    reader.close();

    remove(TEMP_FILE);
}

TEST_CASE("Appending records to a file") {
    remove(TEMP_FILE);

    ObjectFileWriter writer = ObjectFileWriter(TEMP_FILE, sizeof(TestStruct), 1, false);

    TestStruct record_out_1 = { .value = 0xAAAAAAAA };
    TestStruct record_out_2 = { .value = 0xBBBBBBBB };

    CHECK_EQ(writer.open(), true);
    CHECK_EQ(writer.write(&record_out_1), true);
    writer.close();

    ObjectFileWriter appender = ObjectFileWriter(TEMP_FILE, sizeof(TestStruct), 1, true);
    CHECK_EQ(appender.open(), true);
    CHECK_EQ(appender.write(&record_out_2), true);
    appender.close();

    TestStruct record_in;
    ObjectFileReader reader = ObjectFileReader(TEMP_FILE, sizeof(TestStruct));
    CHECK_EQ(reader.open(), true);
    CHECK_EQ(reader.hasNext(), true);
    CHECK_EQ(reader.readNext(&record_in), true);
    CHECK_EQ(record_in.value, 0xAAAAAAAA);
    CHECK_EQ(reader.hasNext(), true);
    CHECK_EQ(reader.readNext(&record_in), true);
    CHECK_EQ(record_in.value, 0xBBBBBBBB);
    CHECK_EQ(reader.hasNext(), false);
    reader.close();

    remove(TEMP_FILE);
}
