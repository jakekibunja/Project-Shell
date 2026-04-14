#pragma once

#include <string>
#include <vector>

// Splits a raw command line into tokens, respecting simple quotes.
std::vector<std::string> parser(const std::string& s);
