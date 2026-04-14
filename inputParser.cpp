#include "inputParser.h"

#include <iomanip>
#include <sstream>

std::vector<std::string> parser(const std::string& s) {
    std::stringstream ss(s);
    std::vector<std::string> inputParsed;
    std::string word;

    // Use std::quoted to keep groups wrapped in "..." together.
    while (ss >> std::quoted(word)) {
        inputParsed.push_back(word);
    }
    return inputParsed;
}
