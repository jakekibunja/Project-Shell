#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include "inputParser.h"
#include "execute.h"

using namespace std;

string getPathFromHome() {
    // Show paths inside HOME as ~/... so the prompt stays short and familiar.
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
        // Prompt always reflects the current working directory.
        cout << "[" << getPathFromHome() << "]$ ";

        // Read one full command line from stdin.
        string input;
        if (!getline(cin, input)) {
            cout << "\n";
            break; // Exit cleanly on EOF (Ctrl-D).
        }

        // Tokenize then dispatch to the shell command handler.
        vector<string> parsedInput = parser(input);
        if (parsedInput.empty()) continue;
        executeCommand(parsedInput);
    }

    return 0;
}
