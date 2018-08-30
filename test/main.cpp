#include "command_test.hpp"
#include "commander.hpp"
#include "reporter.hpp"
#include "snp_test.hpp"
#include "gtest/gtest.h"


int main(int argc, char* argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}