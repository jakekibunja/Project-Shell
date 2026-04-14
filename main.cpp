#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include "inputParser.h"
#include "execute.h"

using namespace std;

string getPathFromHome() {
    string str = filesystem::current_path();
    return str.substr(str.find_last_of("/") + 1);
}

int main() {
    while (true) {
        // Show prompt with current directory
        cout << "[~/" << getPathFromHome() << "]$ ";

        // Take input
        string input;
        getline(cin, input);

        // Parse and execute command
        vector<string> parsedInput = parser(input);
        cout << executeCommand(parsedInput) << endl;
    }

    return 0;
}
