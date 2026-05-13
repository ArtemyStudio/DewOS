#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace c {
    constexpr const char* RESET   = "\033[0m";
    constexpr const char* BOLD    = "\033[1m";
    constexpr const char* DIM     = "\033[2m";
    constexpr const char* GREY    = "\033[90m";
    constexpr const char* ACCENT  = "\033[96m";
    constexpr const char* WARN    = "\033[33m";
    constexpr const char* BAD     = "\033[31m";
}

struct PkgEntry {
    std::string name;
    std::string version;
    std::string description;
};

static std::string repo_dir()      { return "/var/drop/repo"; }
static std::string installed_dir() { return "/var/drop/installed"; }
static std::string index_path()    { return repo_dir() + "/index.txt"; }

static bool path_exists(const std::string& p) { 
    return access(p.c_str(), F_OK) == 0; 
}

static void mkdir_p(const std::string& path) {
    std::string cur;
    std::stringstream ss(path);
    std::string seg;

    if (path.size() > 0 && path[0] == '/') {
        cur = "";
    }

    while (std::getline(ss, seg, '/')) {
        if (seg.empty()) {
            continue;
        }

        cur += "/" + seg;
        mkdir(cur.c_str(), 0755);
    }
}

static int run(const std::vector<std::string>& argv) {
    pid_t pid = fork();

    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        std::vector<char*> args;
        args.reserve(argv.size() + 1);
        for (auto& a : argv) {
            args.push_back(const_cast<char*>(a.c_str()));
        }

        args.push_back(nullptr);
        execvp(args[0], args.data());

        _exit(127);
    }

    int s = 0;
    waitpid(pid, &s, 0);
    if (WIFEXITED(s)) {
        return WEXITSTATUS(s);
    }

    return -1;
}

static std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> v;
    std::string cur;

    for (char c : s) {
        if (c == sep) { 
            v.push_back(cur); cur.clear(); 
        } else {
            cur.push_back(c);
        }
    }

    v.push_back(cur);
    return v;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) {
        return "";
    }

    size_t b = s.find_last_not_of(" \t\r\n");

    return s.substr(a, b - a + 1);
}

static std::vector<PkgEntry> load_index() {
    std::vector<PkgEntry> out;
    std::ifstream f(index_path());

    if (!f.is_open()) {
        return out;
    }

    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);

        if (line.empty() || line[0] == '#') {
            continue;
        }

        auto parts = split(line, '\t');
        if (parts.size() < 2) {
            continue;
        }

        PkgEntry p;
        p.name = trim(parts[0]);
        p.version = trim(parts[1]);

        if (parts.size() >= 3) {
            p.description = trim(parts[2]);
        }

        out.push_back(p);
    }
    return out;
}

static const PkgEntry* find_in_index(const std::vector<PkgEntry>& idx, const std::string& name) {
    for (auto& e : idx) {
        if (e.name == name) {
            return &e;
        }
    }

    return nullptr;
}

static bool is_installed(const std::string& name) {
    return path_exists(installed_dir() + "/" + name);
}

static std::string installed_version(const std::string& name) {
    std::ifstream f(installed_dir() + "/" + name + ".meta");
    if (!f.is_open()) {
        return "";
    }

    std::string v;
    std::getline(f, v);

    return trim(v);
}

static std::vector<std::string> list_installed() {
    std::vector<std::string> out;

    DIR* d = opendir(installed_dir().c_str());
    if (!d) {
        return out;
    }

    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        std::string n = e->d_name;

        if (n == "." || n == "..") {
            continue;
        }

        if (n.size() >= 5 && n.substr(n.size() - 5) == ".meta") {
            continue;
        }

        out.push_back(n);
    }

    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

static int cmd_install(const std::vector<std::string>& names) {
    if (names.empty()) {
        std::cerr << "drop: install needs at least one package name\n";
        return 2;
    }

    auto idx = load_index();
    if (idx.empty()) {
        std::cerr << c::BAD << "drop: " << c::RESET << "repo index is empty or missing.\n";
        std::cerr << "      expected: " << index_path() << "\n";
        return 1;
    }

    mkdir_p(installed_dir());

    int rc = 0;

    for (const auto& name : names) {
        const PkgEntry* e = find_in_index(idx, name);
        if (!e) {
            std::cerr << c::BAD << "drop: " << c::RESET << "no package named '" << name << "' in repo\n";
            rc = 1;

            continue;
        }

        if (is_installed(name)) {
            std::cout << c::GREY << "drop: " << name << " already installed (" << installed_version(name) << ")\n" << c::RESET;

            continue;
        }

        std::string tarball = repo_dir() + "/" + e->name + "-" + e->version + ".drop";
        if (!path_exists(tarball)) {
            std::cerr << c::BAD << "drop: " << c::RESET << "missing tarball: " << tarball << "\n";
            rc = 1;

            continue;
        }

        std::cout << c::ACCENT << "▸ " << c::RESET << "installing " << c::BOLD << name << c::RESET << " " << c::GREY << e->version << c::RESET << "\n";

        std::string list_cmd = "tar -tzf " + tarball;
        FILE* fp = popen(list_cmd.c_str(), "r");

        if (!fp) {
            std::cerr << c::BAD << "drop: " << c::RESET << "tar failed to list\n";
            rc = 1;

            continue;
        }

        std::vector<std::string> files;

        char buf[1024];
        while (fgets(buf, sizeof(buf), fp)) {
            std::string l = trim(std::string(buf));
            if (l.empty() || l == "PKGINFO") {
                continue;
            }

            files.push_back(l);
        }

        int lst = pclose(fp);
        if (lst != 0) {
            std::cerr << c::BAD << "drop: " << c::RESET << "tar listing returned " << lst << "\n";
            rc = 1;

            continue;
        }

        int ext = run({"tar", "-xzf", tarball, "-C", "/", "--exclude=PKGINFO"});
        if (ext != 0) {
            std::cerr << c::BAD << "drop: " << c::RESET << "extraction failed (rc=" << ext << ")\n";
            rc = 1;
            continue;
        }

        {
            std::ofstream f(installed_dir() + "/" + name);
            for (auto& l : files) f << l << "\n";
        } {
            std::ofstream f(installed_dir() + "/" + name + ".meta");
            f << e->version << "\n";
        }

        std::cout << c::ACCENT << "  ✓ " << c::RESET << name << " installed (" << files.size() << " files)\n";
    }

    return rc;
}

static int cmd_remove(const std::vector<std::string>& names) {
    if (names.empty()) {
        std::cerr << "drop: remove needs at least one package name\n";
        return 2;
    }

    int rc = 0;
    for (const auto& name : names) {
        if (!is_installed(name)) {
            std::cerr << c::WARN << "drop: " << c::RESET << name << " is not installed\n";
            rc = 1;

            continue;
        }

        std::ifstream f(installed_dir() + "/" + name);
        std::vector<std::string> files;
        std::string l;

        while (std::getline(f, l)) {
            l = trim(l);
            if (!l.empty()) {
                files.push_back(l);
            }
        }

        f.close();

        std::cout << c::ACCENT << "▸ " << c::RESET << "removing " << c::BOLD << name << c::RESET << "\n";

        std::vector<std::string> dirs;
        for (auto& p : files) {
            std::string full = "/" + p;
            if (!full.empty() && full.back() == '/') {
                full.pop_back();
                dirs.push_back(full);

                continue;
            }

            struct stat st;
            if (lstat(full.c_str(), &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    dirs.push_back(full);
                } else {
                    if (unlink(full.c_str()) != 0 && errno != ENOENT) {
                        std::cerr << c::WARN << "  ! failed to remove " << full << "\n" << c::RESET;
                    }
                }
            }
        }
        
        std::sort(dirs.begin(), dirs.end(), [](const std::string& a, const std::string& b){ 
            return a.size() > b.size(); 
        });

        for (auto& d : dirs) {
            rmdir(d.c_str());
        }

        unlink((installed_dir() + "/" + name).c_str());
        unlink((installed_dir() + "/" + name + ".meta").c_str());

        std::cout << c::ACCENT << "  ✓ " << c::RESET << name << " removed\n";
    }

    return rc;
}

static int cmd_list() {
    auto inst = list_installed();
    if (inst.empty()) {
        std::cout << c::GREY << "no packages installed\n" << c::RESET;

        return 0;
    }

    for (auto& n : inst) {
        std::string v = installed_version(n);
        std::cout << c::BOLD << n << c::RESET << " " << c::GREY << v << c::RESET << "\n";
    }

    return 0;
}

static int cmd_search(const std::vector<std::string>& q) {
    auto idx = load_index();
    if (idx.empty()) {
        std::cerr << c::WARN << "drop: " << c::RESET << "repo index is empty or missing\n";

        return 1;
    }

    std::string query = q.empty() ? "" : q[0];
    for (auto& e : idx) {
        if (!query.empty()) {
            if (e.name.find(query) == std::string::npos && e.description.find(query) == std::string::npos) {
                continue;
            }
        }
        
        std::string marker = is_installed(e.name) ? (std::string(c::ACCENT) + "[installed] " + c::RESET) : "";
        std::cout << marker << c::BOLD << e.name << c::RESET << " " << c::GREY << e.version << c::RESET;

        if (!e.description.empty()) {
            std::cout << " — " << e.description;
        }

        std::cout << "\n";
    }

    return 0;
}

static int cmd_update() {
    if (!path_exists(index_path())) {
        std::cerr << c::WARN << "drop: " << c::RESET << "no repo index at " << index_path() << "\n";

        return 1;
    }

    auto idx = load_index();
    std::cout << c::ACCENT << "▸ " << c::RESET << "local repo: " << idx.size() << " packages\n";

    return 0;
}

static void usage() {
    std::cout <<
        c::BOLD << "drop" << c::RESET << " — DewOS package manager\n\n"
        "  drop list                show installed packages\n"
        "  drop search [query]      list packages in the repo\n"
        "  drop install <pkg>...    install one or more packages\n"
        "  drop remove <pkg>...     uninstall packages\n"
        "  drop update              refresh repo index\n"
        "  drop help                this message\n";
}

int main(int argc, char** argv) {
    mkdir_p(installed_dir());

    if (argc < 2) { 
        usage(); return 0; 
    }

    std::string cmd = argv[1];
    std::vector<std::string> rest;

    for (int i = 2; i < argc; ++i) {
        rest.push_back(argv[i]);
    }

    if (cmd == "list" || cmd == "ls") return cmd_list();
    if (cmd == "search")               return cmd_search(rest);
    if (cmd == "install" || cmd == "add" || cmd == "i") return cmd_install(rest);
    if (cmd == "remove"  || cmd == "rm" || cmd == "r") return cmd_remove(rest);
    if (cmd == "update"  || cmd == "up") return cmd_update();
    if (cmd == "help" || cmd == "-h" || cmd == "--help") { usage(); return 0; }

    std::cerr << "drop: unknown command: " << cmd << "\n";
    usage();
    return 2;
}