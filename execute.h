#pragma once

#include <string>
#include <vector>

// Executes a parsed command line.
// Returns the child exit status (0 for success) or a negative value on immediate failure.
int executeCommand(const std::vector<std::string>& input);
