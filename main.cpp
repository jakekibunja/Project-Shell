#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include "inputParser.h"
#include "execute.h"

using namespace std;

string getPathFromHome() {
    auto cwd = filesystem::current_path();
    const char* home = getenv("HOME");
    if (home) {
        string homeStr(home);
        auto pos = cwd.string().find(homeStr);
        if (pos == 0) {
            string rel = cwd.string().substr(homeStr.size());
            if (rel.empty()) return "~";
            if (rel[0] != '/') rel = "/" + rel;
            return "~" + rel;
        }
    }
    return cwd.string();
}

int main() {
    while (true) {
        // Show prompt with current directory
        cout << "[" << getPathFromHome() << "]$ ";

        // Take input
        string input;
        if (!getline(cin, input)) {
            cout << "\n";
            break; // EOF (Ctrl-D)
        }

        // Parse and execute command
        vector<string> parsedInput = parser(input);
        if (parsedInput.empty()) continue;
        executeCommand(parsedInput);
    }

    return 0;
}
