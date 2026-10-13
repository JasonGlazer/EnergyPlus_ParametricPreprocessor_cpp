// ParametricPreprocessor - command line entry point.
//
// Usage: ParametricPreprocessor <input file with path>.idf
//
// Creates one IDF file per parametric case named <input file>-<suffix>.idf next to the
// input file, plus <input file>.err when errors or warnings are found.

#include "ParametricPreprocessor.hh"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
    std::cout << "ParametricPreprocessor Started.\n";
    auto const startTime = std::chrono::steady_clock::now();

    std::string const inputFilePathName = (argc > 1) ? argv[1] : "";

    // the DeveloperFlag environment variable turns on more displays to the console
    bool developerFlag = false;
    if (char const *envValue = std::getenv("DeveloperFlag")) {
        std::string const value(envValue);
        std::size_t const first = value.find_first_not_of(' ');
        if (first != std::string::npos) developerFlag = (std::toupper(static_cast<unsigned char>(value[first])) == 'Y');
    }

    int numErrMsgs = 0;
    try {
        ParametricPreprocessor::Preprocessor preprocessor(developerFlag);
        numErrMsgs = preprocessor.run(inputFilePathName);
    } catch (std::exception const &e) {
        std::cout << "ParametricPreprocessor internal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    double const elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
    char timeText[32];
    std::snprintf(timeText, sizeof(timeText), "%10.3f", elapsed);
    if (numErrMsgs > 0) {
        std::cout << "ParametricPreprocessor Finished with Error(s). Time:" << timeText << '\n';
    } else {
        std::cout << "ParametricPreprocessor Finished. Time:" << timeText << '\n';
    }
    // Flush explicitly: when stdout is redirected, leaving the flush to process exit lost
    // all console output in a few percent of runs on Windows.
    std::cout.flush();
    if (developerFlag) {
        std::cout << "Type any key and press ENTER to exit" << std::endl;
        std::string keyIn;
        std::getline(std::cin, keyIn);
    }
    return EXIT_SUCCESS;
}
