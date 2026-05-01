#include "execute.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <grp.h>
#include <iostream>
#include <optional>
#include <pwd.h>
#include <regex>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace {

// Centralized help text so "help" output and supported commands stay in sync.
std::vector<std::string> helpLines() {
    return {
        "Supported commands:",
        "  cd [dir]                Change directory, or print current directory",
        "  clr | cls               Clear the terminal screen",
        "  dir [path]              List directory contents (default: current dir)",
        "  environ                 Display environment variables",
        "  echo [text ...]         Print text with normalized spacing",
        "  help                    Display this help (paged like more)",
        "  pause                   Wait until Enter is pressed",
        "  quit | exit [code]      Exit the shell",
        "  chmod <mode> <paths...> Change file permissions (octal mode)",
        "  chown <owner[:group]> <paths...> Change file owner/group",
        "  ls [path]               List files/directories",
        "  pwd                     Print working directory",
        "  cat <files...>          Print file contents",
        "  mkdir <dirs...>         Create directories",
        "  rmdir <dirs...>         Remove empty directories",
        "  rm <paths...>           Remove files or directories",
        "  cp <src...> <dest>      Copy files/directories",
        "  mv <src...> <dest>      Move files/directories",
        "  touch <files...>        Create file or update timestamp",
        "  grep <pattern> [files...] Search regex pattern in input/files",
        "  wc [files...]           Count lines, words, characters",
    };
}

void printPaged(const std::vector<std::string>& lines, std::size_t pageSize = 20) {
    std::size_t shown = 0;
    for (const auto& line : lines) {
        std::cout << line << '\n';
        ++shown;
        if (shown % pageSize == 0 && shown < lines.size()) {
            std::cout << "--More--";
            std::string dummy;
            std::getline(std::cin, dummy);
        }
    }
}

void printHelp() {
    printPaged(helpLines());
}

void printSystemError(const std::string& prefix) {
    std::cerr << prefix << ": " << std::strerror(errno) << '\n';
}

void printFsError(const std::string& prefix, const std::filesystem::path& p, const std::error_code& ec) {
    std::cerr << prefix << " '" << p.string() << "': " << ec.message() << '\n';
}

int cmdPwd() {
    char buf[PATH_MAX];
    if (getcwd(buf, sizeof(buf))) {
        std::cout << buf << '\n';
        return 0;
    }
    printSystemError("pwd");
    return -1;
}

int cmdCd(const std::vector<std::string>& input) {
    if (input.size() == 1) return cmdPwd();
    if (input.size() > 2) {
        std::cerr << "cd: usage: cd [directory]\n";
        return -1;
    }

    if (chdir(input[1].c_str()) != 0) {
        printSystemError("cd");
        return -1;
    }

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) {
        // Keep PWD aligned with the real cwd for commands that rely on env vars.
        setenv("PWD", cwd, 1);
    }
    return 0;
}

int cmdClear() {
    std::cout << "\033[2J\033[H";
    return 0;
}

int listDirectory(const std::string& rawPath) {
    const std::filesystem::path path = rawPath.empty() ? "." : rawPath;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        printFsError("ls", path, ec ? ec : std::make_error_code(std::errc::no_such_file_or_directory));
        return -1;
    }
    if (!std::filesystem::is_directory(path, ec)) {
        std::cerr << "ls: '" << path.string() << "' is not a directory\n";
        return -1;
    }

    std::vector<std::string> entries;
    for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
        if (ec) {
            printFsError("ls", path, ec);
            return -1;
        }
        std::string name = entry.path().filename().string();
        // Add a trailing slash so folders are easy to spot in plain output.
        if (entry.is_directory(ec) && !ec) name += "/";
        entries.push_back(std::move(name));
    }
    // Sort for stable output regardless of filesystem iteration order.
    std::sort(entries.begin(), entries.end());
    for (const auto& name : entries) {
        std::cout << name << '\n';
    }
    return 0;
}

int cmdLsLike(const std::vector<std::string>& input, const std::string& commandName) {
    if (input.size() > 2) {
        std::cerr << commandName << ": usage: " << commandName << " [directory]\n";
        return -1;
    }
    return listDirectory(input.size() == 2 ? input[1] : ".");
}

int cmdEnviron() {
    for (char** env = environ; *env != nullptr; ++env) {
        std::cout << *env << '\n';
    }
    return 0;
}

int cmdEcho(const std::vector<std::string>& input) {
    for (std::size_t i = 1; i < input.size(); ++i) {
        if (i > 1) std::cout << ' ';
        std::cout << input[i];
    }
    std::cout << '\n';
    return 0;
}

int cmdPause() {
    std::cout << "Press Enter to continue...";
    std::string dummy;
    std::getline(std::cin, dummy);
    return 0;
}

[[noreturn]] void cmdQuit(const std::vector<std::string>& input) {
    int code = 0;
    if (input.size() > 1) code = std::atoi(input[1].c_str());
    std::exit(code);
}

std::optional<uid_t> parseUid(const std::string& token) {
    if (token.empty()) return std::nullopt;
    // Accept either raw numeric IDs or account names.
    bool allDigits = std::all_of(token.begin(), token.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
    if (allDigits) return static_cast<uid_t>(std::stoul(token));
    if (const passwd* pw = getpwnam(token.c_str())) return pw->pw_uid;
    return std::nullopt;
}

std::optional<gid_t> parseGid(const std::string& token) {
    if (token.empty()) return std::nullopt;
    // Accept either raw numeric IDs or group names.
    bool allDigits = std::all_of(token.begin(), token.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
    if (allDigits) return static_cast<gid_t>(std::stoul(token));
    if (const group* gr = getgrnam(token.c_str())) return gr->gr_gid;
    return std::nullopt;
}

int cmdChmod(const std::vector<std::string>& input) {
    if (input.size() < 3) {
        std::cerr << "chmod: usage: chmod <octal_mode> <path...>\n";
        return -1;
    }

    char* end = nullptr;
    errno = 0;
    // Parse explicit octal mode (e.g. 755, 644).
    unsigned long mode = std::strtoul(input[1].c_str(), &end, 8);
    if (errno != 0 || end == input[1].c_str() || *end != '\0') {
        std::cerr << "chmod: invalid mode '" << input[1] << "'\n";
        return -1;
    }

    int rc = 0;
    for (std::size_t i = 2; i < input.size(); ++i) {
        if (::chmod(input[i].c_str(), static_cast<mode_t>(mode)) != 0) {
            printSystemError("chmod");
            rc = -1;
        }
    }
    return rc;
}

int cmdChown(const std::vector<std::string>& input) {
    if (input.size() < 3) {
        std::cerr << "chown: usage: chown <owner[:group]> <path...>\n";
        return -1;
    }

    const std::string& spec = input[1];
    std::string owner = spec;
    std::string groupName;
    const std::size_t colon = spec.find(':');
    // Supports owner-only, group-only (:group), or owner:group forms.
    if (colon != std::string::npos) {
        owner = spec.substr(0, colon);
        groupName = spec.substr(colon + 1);
    }

    uid_t uid = static_cast<uid_t>(-1);
    gid_t gid = static_cast<gid_t>(-1);
    bool setUid = false;
    bool setGid = false;

    if (!owner.empty()) {
        auto parsedUid = parseUid(owner);
        if (!parsedUid.has_value()) {
            std::cerr << "chown: unknown owner '" << owner << "'\n";
            return -1;
        }
        uid = *parsedUid;
        setUid = true;
    }

    if (!groupName.empty()) {
        auto parsedGid = parseGid(groupName);
        if (!parsedGid.has_value()) {
            std::cerr << "chown: unknown group '" << groupName << "'\n";
            return -1;
        }
        gid = *parsedGid;
        setGid = true;
    }

    if (!setUid && !setGid) {
        std::cerr << "chown: owner and/or group is required\n";
        return -1;
    }

    int rc = 0;
    for (std::size_t i = 2; i < input.size(); ++i) {
        if (::chown(input[i].c_str(), setUid ? uid : static_cast<uid_t>(-1), setGid ? gid : static_cast<gid_t>(-1)) != 0) {
            printSystemError("chown");
            rc = -1;
        }
    }
    return rc;
}

int cmdCat(const std::vector<std::string>& input) {
    if (input.size() < 2) {
        // No file provided: behave like a filter and echo stdin.
        std::string line;
        while (std::getline(std::cin, line)) std::cout << line << '\n';
        return 0;
    }

    int rc = 0;
    for (std::size_t i = 1; i < input.size(); ++i) {
        std::ifstream file(input[i], std::ios::in);
        if (!file) {
            std::cerr << "cat: cannot open '" << input[i] << "'\n";
            rc = -1;
            continue;
        }
        std::cout << file.rdbuf();
    }
    return rc;
}

int cmdMkdir(const std::vector<std::string>& input) {
    if (input.size() < 2) {
        std::cerr << "mkdir: usage: mkdir <directory...>\n";
        return -1;
    }

    int rc = 0;
    for (std::size_t i = 1; i < input.size(); ++i) {
        std::error_code ec;
        std::filesystem::create_directories(input[i], ec);
        if (ec) {
            printFsError("mkdir", input[i], ec);
            rc = -1;
        }
    }
    return rc;
}

int cmdRmdir(const std::vector<std::string>& input) {
    if (input.size() < 2) {
        std::cerr << "rmdir: usage: rmdir <directory...>\n";
        return -1;
    }

    int rc = 0;
    for (std::size_t i = 1; i < input.size(); ++i) {
        std::error_code ec;
        bool removed = std::filesystem::remove(input[i], ec);
        if (ec || !removed) {
            if (!ec) ec = std::make_error_code(std::errc::directory_not_empty);
            printFsError("rmdir", input[i], ec);
            rc = -1;
        }
    }
    return rc;
}

int cmdRm(const std::vector<std::string>& input) {
    if (input.size() < 2) {
        std::cerr << "rm: usage: rm <path...>\n";
        return -1;
    }

    int rc = 0;
    for (std::size_t i = 1; i < input.size(); ++i) {
        const std::filesystem::path p = input[i];
        std::error_code ec;
        if (!std::filesystem::exists(p, ec)) {
            std::cerr << "rm: cannot remove '" << p.string() << "': no such file or directory\n";
            rc = -1;
            continue;
        }

        if (std::filesystem::is_directory(p, ec)) {
            std::filesystem::remove_all(p, ec);
        } else {
            std::filesystem::remove(p, ec);
        }
        if (ec) {
            printFsError("rm", p, ec);
            rc = -1;
        }
    }
    return rc;
}

std::filesystem::path copyTargetForSource(
    const std::filesystem::path& src,
    const std::filesystem::path& dest,
    bool destinationIsDirectory) {
    // For multi-source copies, mimic cp semantics: place each src under dest dir.
    if (destinationIsDirectory) return dest / src.filename();
    return dest;
}

int copyPath(const std::filesystem::path& src, const std::filesystem::path& dst) {
    std::error_code ec;
    if (std::filesystem::is_directory(src, ec)) {
        std::filesystem::copy(
            src,
            dst,
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
            ec);
    } else {
        std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
    }

    if (ec) {
        printFsError("cp", src, ec);
        return -1;
    }
    return 0;
}

int cmdCp(const std::vector<std::string>& input) {
    if (input.size() < 3) {
        std::cerr << "cp: usage: cp <source...> <destination>\n";
        return -1;
    }

    std::vector<std::filesystem::path> sources;
    for (std::size_t i = 1; i + 1 < input.size(); ++i) sources.push_back(input[i]);
    const std::filesystem::path destination = input.back();

    std::error_code ec;
    const bool destinationIsDirectory = std::filesystem::is_directory(destination, ec);
    if (sources.size() > 1 && !destinationIsDirectory) {
        std::cerr << "cp: destination must be a directory when copying multiple sources\n";
        return -1;
    }

    int rc = 0;
    for (const auto& src : sources) {
        if (!std::filesystem::exists(src, ec)) {
            std::cerr << "cp: cannot stat '" << src.string() << "'\n";
            rc = -1;
            continue;
        }
        const std::filesystem::path target = copyTargetForSource(src, destination, destinationIsDirectory);
        if (copyPath(src, target) != 0) rc = -1;
    }
    return rc;
}

int movePath(const std::filesystem::path& src, const std::filesystem::path& dst) {
    std::error_code ec;
    std::filesystem::rename(src, dst, ec);
    if (!ec) return 0;

    // rename() can fail across filesystems; fallback to copy + delete.
    if (copyPath(src, dst) != 0) return -1;

    if (std::filesystem::is_directory(src, ec)) {
        std::filesystem::remove_all(src, ec);
    } else {
        std::filesystem::remove(src, ec);
    }
    if (ec) {
        printFsError("mv", src, ec);
        return -1;
    }
    return 0;
}

int cmdMv(const std::vector<std::string>& input) {
    if (input.size() < 3) {
        std::cerr << "mv: usage: mv <source...> <destination>\n";
        return -1;
    }

    std::vector<std::filesystem::path> sources;
    for (std::size_t i = 1; i + 1 < input.size(); ++i) sources.push_back(input[i]);
    const std::filesystem::path destination = input.back();

    std::error_code ec;
    const bool destinationIsDirectory = std::filesystem::is_directory(destination, ec);
    if (sources.size() > 1 && !destinationIsDirectory) {
        std::cerr << "mv: destination must be a directory when moving multiple sources\n";
        return -1;
    }

    int rc = 0;
    for (const auto& src : sources) {
        if (!std::filesystem::exists(src, ec)) {
            std::cerr << "mv: cannot stat '" << src.string() << "'\n";
            rc = -1;
            continue;
        }
        const std::filesystem::path target = copyTargetForSource(src, destination, destinationIsDirectory);
        if (movePath(src, target) != 0) rc = -1;
    }
    return rc;
}

int cmdTouch(const std::vector<std::string>& input) {
    if (input.size() < 2) {
        std::cerr << "touch: usage: touch <file...>\n";
        return -1;
    }

    int rc = 0;
    for (std::size_t i = 1; i < input.size(); ++i) {
        const std::string& path = input[i];
        int fd = open(path.c_str(), O_CREAT | O_WRONLY, 0666);
        if (fd < 0) {
            printSystemError("touch");
            rc = -1;
            continue;
        }
        close(fd);

        // Update both access and modification timestamps to "now".
        timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        timespec times[2] = {now, now};
        if (utimensat(AT_FDCWD, path.c_str(), times, 0) != 0) {
            printSystemError("touch");
            rc = -1;
        }
    }
    return rc;
}

bool grepLineMatches(const std::regex& pattern, const std::string& line) {
    return std::regex_search(line, pattern);
}

int grepStream(const std::regex& pattern, std::istream& in, const std::string& prefix) {
    std::string line;
    while (std::getline(in, line)) {
        if (grepLineMatches(pattern, line)) {
            // Prefix with filename when scanning multiple files.
            if (!prefix.empty()) std::cout << prefix << ':';
            std::cout << line << '\n';
        }
    }
    return 0;
}

int cmdGrep(const std::vector<std::string>& input) {
    if (input.size() < 2) {
        std::cerr << "grep: usage: grep <pattern> [file...]\n";
        return -1;
    }

    std::regex pattern;
    try {
        pattern = std::regex(input[1]);
    } catch (const std::regex_error&) {
        std::cerr << "grep: invalid regex '" << input[1] << "'\n";
        return -1;
    }

    if (input.size() == 2) {
        return grepStream(pattern, std::cin, "");
    }

    const bool withPrefix = (input.size() > 3);
    int rc = 0;
    for (std::size_t i = 2; i < input.size(); ++i) {
        std::ifstream file(input[i]);
        if (!file) {
            std::cerr << "grep: cannot open '" << input[i] << "'\n";
            rc = -1;
            continue;
        }
        grepStream(pattern, file, withPrefix ? input[i] : "");
    }
    return rc;
}

struct WcCounts {
    std::size_t lines = 0;
    std::size_t words = 0;
    std::size_t chars = 0;
};

WcCounts wcStream(std::istream& in) {
    WcCounts counts;
    std::string line;
    while (std::getline(in, line)) {
        ++counts.lines;
        counts.chars += line.size() + 1; // Count newline so output matches common wc behavior.
        std::istringstream iss(line);
        std::string word;
        while (iss >> word) ++counts.words;
    }
    return counts;
}

void printWc(const WcCounts& counts, const std::string& label) {
    std::cout << counts.lines << ' ' << counts.words << ' ' << counts.chars;
    if (!label.empty()) std::cout << ' ' << label;
    std::cout << '\n';
}

int cmdWc(const std::vector<std::string>& input) {
    if (input.size() == 1) {
        printWc(wcStream(std::cin), "");
        return 0;
    }

    int rc = 0;
    WcCounts total{};
    for (std::size_t i = 1; i < input.size(); ++i) {
        std::ifstream file(input[i]);
        if (!file) {
            std::cerr << "wc: cannot open '" << input[i] << "'\n";
            rc = -1;
            continue;
        }
        WcCounts counts = wcStream(file);
        total.lines += counts.lines;
        total.words += counts.words;
        total.chars += counts.chars;
        printWc(counts, input[i]);
    }
    if (input.size() > 2) printWc(total, "total");
    return rc;
}

} // namespace

int executeCommand(const std::vector<std::string>& input) {
    if (input.empty()) return 0;

    const std::string& cmd = input[0];

    // Simple dispatch table via if-chain for built-in commands.
    if (cmd == "cd") return cmdCd(input);
    if (cmd == "pwd") return cmdPwd();
    if (cmd == "clr" || cmd == "cls") return cmdClear();
    if (cmd == "dir") return cmdLsLike(input, "dir");
    if (cmd == "ls") return cmdLsLike(input, "ls");
    if (cmd == "environ") return cmdEnviron();
    if (cmd == "echo") return cmdEcho(input);
    if (cmd == "help") {
        printHelp();
        return 0;
    }
    if (cmd == "pause") return cmdPause();
    if (cmd == "quit" || cmd == "exit") cmdQuit(input);
    if (cmd == "chmod") return cmdChmod(input);
    if (cmd == "chown") return cmdChown(input);
    if (cmd == "cat") return cmdCat(input);
    if (cmd == "mkdir") return cmdMkdir(input);
    if (cmd == "rmdir") return cmdRmdir(input);
    if (cmd == "rm") return cmdRm(input);
    if (cmd == "cp") return cmdCp(input);
    if (cmd == "mv") return cmdMv(input);
    if (cmd == "touch") return cmdTouch(input);
    if (cmd == "grep") return cmdGrep(input);
    if (cmd == "wc") return cmdWc(input);

    std::cerr << "Unknown command: " << cmd << '\n';
    return -1;
}
