#include "../TactilityCore/TestFile.h"
#include "doctest.h"

#include <../../Tactility/Include/Tactility/file/PropertiesFile.h>

using namespace tt;

TEST_CASE("loadPropertiesFile() should return false when the file does not exist") {
    std::map<std::string, std::string> properties;
    CHECK_EQ(file::loadPropertiesFile("does_not_exist.properties", properties), false);
}

TEST_CASE("PropertiesFile should parse a valid file properly") {
    TestFile file("test.properties");
    file.writeData(
        "# Comment\n" // Regular comment
        " \t# Comment\n" // Prefixed comment
        "key1=value1\n" // Regular property
        " \tkey 2\t = \tvalue 2\t " // Property with empty space
    );

    std::map<std::string, std::string> properties;

    // Load data
    CHECK_EQ(file::loadPropertiesFile(file.getPath(), properties), true);

    CHECK_EQ(properties.size(), 2);
    CHECK_EQ(properties["key1"], "value1");
    CHECK_EQ(properties["key 2"], "value 2");
}
