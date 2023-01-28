#include "argParser.hpp"

int main(int argc, char** argv) {
    argParser::options serverOptions{};
    if (!argParser::parseArgs(argc, argv, serverOptions)) return 0;

    std::cout << "Account disabled: " << serverOptions.no_account << "\n";

    return 0;
}
