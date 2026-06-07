// test_main.cpp
#define CATCH_CONFIG_RUNNER
#include <catch2/catch_all.hpp>

// Global tracking structure to capture test summary data safely
struct RunSummary {
    unsigned int failedAssertions = 0;
    unsigned int skippedTestCases = 0;
    unsigned int executedTestCases = 0;
} runSummary;

// Create an EventListener to automatically capture Catch2 test run statistics
struct TestInterceptor : Catch::EventListenerBase {
    using EventListenerBase::EventListenerBase;

    // This hook runs automatically when the entire test execution completes
    void testRunEnded(Catch::TestRunStats const& testRunStats) override {
        runSummary.failedAssertions = testRunStats.totals.assertions.failed;
        runSummary.skippedTestCases = testRunStats.totals.testCases.skipped;
        runSummary.executedTestCases = testRunStats.totals.testCases.total() - runSummary.skippedTestCases;
    }
};

// Register listener with Catch2's internal registry
CATCH_REGISTER_LISTENER(TestInterceptor)

int main(int argc, char* argv[]) {
    Catch::Session session;

    int returnCode = session.applyCommandLine(argc, argv);
    if (returnCode != 0) {
        return returnCode;
    }

    // Run the isolated test file
    int result = session.run();

    // If any assersions failed, return 1 to indicate a failure to Ctest. This
    // exists for a consistent return code to represent failures. Ctest treats a
    // return code of four as a skipped test. Thus, four failed checks would be
    // shown as a skipped test.
    if (runSummary.failedAssertions > 0) return 1;

    return result;
}
