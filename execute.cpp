#include "execute.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace {

void printHelp() {
    std::cout << "Built-ins:\n"
              << "  cd <dir>      : change directory (default $HOME)\n"
              << "  pwd           : print working directory\n"
              << "  echo <args>   : print arguments\n"
              << "  environ       : list environment variables\n"
              << "  cls           : clear screen\n"
              << "  help          : show this help\n"
              << "  exit          : exit the shell\n"
              << "External commands fallback to execvp (ls, mkdir, rm, ...).\n";
}

int runExternal(const std::vector<std::string>& input) {
    // Allocate argv array with stable storage.
    std::vector<char*> argv;
    argv.reserve(input.size() + 1);
    for (const auto& token : input) {
        argv.push_back(const_cast<char*>(token.c_str()));
    }
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid == 0) {
        execvp(argv[0], argv.data());
        perror("execvp");
        _exit(127);
    }
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return status;
}

} // namespace

int executeCommand(const std::vector<std::string>& input) {
    if (input.empty()) return 0;

    const std::string& cmd = input[0];

    if (cmd == "cd") {
        const char* path = nullptr;
        if (input.size() > 1) {
            path = input[1].c_str();
        } else {
            path = std::getenv("HOME");
        }
        if (!path) {
            std::cerr << "cd: HOME not set\n";
            return -1;
        }
        if (chdir(path) != 0) {
            perror("cd");
            return -1;
        }
        return 0;
    }

    if (cmd == "pwd") {
        char buf[PATH_MAX];
        if (getcwd(buf, sizeof(buf))) {
            std::cout << buf << "\n";
            return 0;
        }
        perror("pwd");
        return -1;
    }

    if (cmd == "echo") {
        for (size_t i = 1; i < input.size(); ++i) {
            if (i > 1) std::cout << ' ';
            std::cout << input[i];
        }
        std::cout << "\n";
        return 0;
    }

    if (cmd == "environ") {
        for (char** env = environ; *env != nullptr; ++env) {
            std::cout << *env << "\n";
        }
        return 0;
    }

    if (cmd == "cls") {
        std::cout << "\033[2J\033[H";
        return 0;
    }

    if (cmd == "help") {
        printHelp();
        return 0;
    }

    if (cmd == "exit") {
        // Allow optional exit status
        int code = 0;
        if (input.size() > 1) {
            code = std::atoi(input[1].c_str());
        }
        std::exit(code);
    }

    // Fallback to external command
    return runExternal(input);
}
