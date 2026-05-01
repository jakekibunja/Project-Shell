#include "inputParser.h"

#include <iomanip>
#include <sstream>

std::vector<std::string> parser(const std::string& s) {
    // Stringstream gives us simple shell-like token splitting on whitespace.
    std::stringstream ss(s);
    std::vector<std::string> inputParsed;
    std::string word;

    // std::quoted keeps "two words" as one token and strips the quotes.
    while (ss >> std::quoted(word)) {
        inputParsed.push_back(word);
    }
    return inputParsed;
}
