#include "../include/shell.hpp"
#include "../include/system.hpp"
#include "../include/network.hpp"
#include "../include/colors.hpp"
#include "../include/init.hpp"

#include <cerrno>
#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstdio>

#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/utsname.h>

termios original_termios;
std::vector<std::string> history;
size_t history_index = 0;

struct ShellOutput {
    size_t row = 0;
    size_t col = 0;
    bool escape_sequence = false;
};

static ShellOutput shell_output;

void shell_loop() {
    std::vector<Command> commands = create_commands();

    while (true) {
        std::string input = read_line(make_prompt());

        CommandContext ctx = parse_command(input);

        if (ctx.name == "help") {
            cmd_help(ctx, commands);
            continue;
        }

        exec_command(ctx, commands);
    }
}

CommandContext parse_command(const std::string& input) {
    CommandContext ctx;
    ctx.raw = input;

    std::stringstream ss(input);
    std::string word;

    if (ss >> word) {
        ctx.name = word;
    }

    while (ss >> word) {
        if (word[0] == '-') {
            ctx.flags.push_back(word);
        } else {
            ctx.args.push_back(word);
        }
    }

    return ctx;
}

std::vector<Command> create_commands() {
    std::vector<Command> commands;

    commands.push_back(Command{"about", "Display information about DewOS", cmd_about});
    commands.push_back(Command{"poweroff", "Power off the system", sys_poweroff});
    commands.push_back(Command{"reboot", "Reboot the system", sys_reboot});
    commands.push_back(Command{"clear", "Clear the console screen", cmd_clear});
    commands.push_back(Command{"echo", "Print the provided text to the console", cmd_echo});
    commands.push_back(Command{"pwd", "Print the current working directory", cmd_pwd});
    commands.push_back(Command{"ls", "List files in the current directory", cmd_ls});
    commands.push_back(Command{"cd", "Change the current directory", cmd_cd});
    commands.push_back(Command{"mkdir", "Create a new directory", cmd_mkdir});
    commands.push_back(Command{"touch", "Create a new empty file or update timestamp of existing file", cmd_touch});
    commands.push_back(Command{"rm", "Remove a file or directory (with flag -r for recursive)", cmd_rm});
    commands.push_back(Command{"cat", "Display contents of a file", cmd_cat});
    commands.push_back(Command{"kilo", "Open a file in the kilo text editor", cmd_kilo});
    commands.push_back(Command{"dewfetch", "Print system information", cmd_dewfetch});
    commands.push_back(Command{"ps", "List running processes", cmd_ps});
    commands.push_back(Command{"whoami", "Print current logged in user", cmd_whoami});
    commands.push_back(Command{"id", "Print current user id", cmd_id});
    commands.push_back(Command{"install", "Start DewOS installer", cmd_install});
    commands.push_back(Command{"sh", "Start BusyBox shell", cmd_sh});
    commands.push_back(Command{"mount", "Mount path", cmd_mount});
    commands.push_back(Command{"umount", "Unmount path", cmd_umount});
    commands.push_back(Command{"su", "Switch user", cmd_su});
    commands.push_back(Command{"netlog", "Show network startup log", cmd_netlog});
    commands.push_back(Command{"wifi-detect", "Detect Wi-Fi interfaces and tools", cmd_wifi_detect});
    commands.push_back(Command{"wifi-scan", "Scan Wi-Fi networks", cmd_wifi_scan});
    commands.push_back(Command{"wifi-connect-debug", "Connect Wi-Fi with debug output", cmd_wifi_connect_debug});
    commands.push_back(Command{"net-test", "Run network connectivity checks", cmd_net_test});
    commands.push_back(Command{"network", "Network status, scan, DHCP and debug connect", cmd_network});
    commands.push_back(Command{"drop", "Package manager (drop install/remove/list/search)", cmd_drop});

    return commands;
}

void exec_command(const CommandContext& ctx, const std::vector<Command>& commands) {
    if (ctx.name.empty()) {
        return;
    }

    for (const auto& cmd : commands) {
        if (cmd.name == ctx.name) {
            cmd.handler(ctx);
            return;
        }
    }

    unknown_command(ctx);
}

void cmd_about(const CommandContext& ctx) {
    (void)ctx;

    shell_print("DewOS is a simple Linux distribution\n");
    shell_print("Author: ArtemyStudio (artemystudio.co@gmail.com)\n");
}


void cmd_help(const CommandContext& ctx, const std::vector<Command>& commands) {
    (void)ctx;

    shell_print("Available commands:\n");
    shell_print("  help - Display available commands\n");

    for (const Command& cmd : commands) {
        shell_print("  " + cmd.name + " - " + cmd.description + "\n");
    }
}

void cmd_clear(const CommandContext& ctx) {
    (void)ctx;
    shell_print("\033[2J\033[H");
}

void cmd_echo(const CommandContext& ctx) {
    for (const std::string& arg : ctx.args) {
        shell_print(arg + " ");
    }

    shell_print("\n");
}

void cmd_install(const CommandContext& ctx) {
    (void)ctx;

    shell_print("[DewOS] Starting installer...\n");

    disable_raw_mode();
    raw_write("\033[?25h");

    pid_t pid = fork();

    if (pid < 0) {
        shell_print("[DewOS] fork failed\n");
        enable_raw_mode();
        raw_write("\033[?25l");
        return;
    }

    if (pid == 0) {
        const char* path = "/sbin/dew-install";
        char* const args[] = {
            (char*)path,
            NULL
        };

        execv(path, args);

        perror("execv /sbin/dew-install failed");
        _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, 0);

    enable_raw_mode();
    raw_write("\033[?25l");

    shell_print("\n[DewOS] Installer exited.\n");

    rootfs_ready = detect_rootfs();
}

void print_banner() {
    shell_print("██████╗ ███████╗██╗    ██╗ ██████╗ ███████╗\n");
    shell_print("██╔══██╗██╔════╝██║    ██║██╔═══██╗██╔════╝\n");
    shell_print("██║  ██║█████╗  ██║ █╗ ██║██║   ██║███████╗\n");
    shell_print("██║  ██║██╔══╝  ██║███╗██║██║   ██║╚════██║\n");
    shell_print("██████╔╝███████╗╚███╔███╔╝╚██████╔╝███████║\n");
    shell_print("╚═════╝ ╚══════╝ ╚══╝╚══╝  ╚═════╝ ╚══════╝\n");
    shell_print("\n");
}

void cmd_pwd(const CommandContext& ctx) {
    (void)ctx;

    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        shell_print(std::string(cwd) + "\n");
    } else {
        print_error("Failed to get current working directory");
    }
}

void cmd_ls(const CommandContext& ctx) {
    (void)ctx;

    std::string path = ctx.args.empty() ? "." : ctx.args[0];

    DIR* dir = opendir(path.c_str());
    if (!dir) {
        print_error("ls: failed to open " + path);
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        shell_print(std::string(entry->d_name) + "  ");
    }

    shell_print("\n");
    closedir(dir);
}

void cmd_cd(const CommandContext& ctx) {
    if (ctx.args.empty()) {
        print_error("No directory specified");
        return;
    }

    if (chdir(ctx.args[0].c_str()) < 0) {
        print_error("Failed to change directory");
    }
}

void cmd_mkdir(const CommandContext& ctx) {
    if (ctx.args.empty()) {
        print_error("No directory name specified");
        return;
    }

    if (mkdir(ctx.args[0].c_str(), 0755) < 0) {
        print_error("Failed to create directory");
    }
}

void cmd_touch(const CommandContext& ctx) {
    if (ctx.args.empty()) {
        print_error("No file name specified");
        return;
    }

    int fd = open(ctx.args[0].c_str(), O_CREAT | O_WRONLY, 0644);
    if (fd < 0) {
        print_error("Failed to create file");
        return;
    }
    close(fd);
}

void cmd_rm(const CommandContext& ctx) {
    if (ctx.args.empty()) {
        print_error("No file or directory specified");
        return;
    }

    const std::string& path = ctx.args[0];

    if (ctx.has_flag("-r") || ctx.has_flag("-rf") || ctx.has_flag("-fr")) {
        disable_raw_mode();
        pid_t pid = fork();
        if (pid == 0) {
            execl("/bin/rm", "rm", "-rf", path.c_str(), nullptr);
            _exit(127);
        }
        if (pid > 0) {
            int status = 0;
            waitpid(pid, &status, 0);
        }
        enable_raw_mode();
        return;
    }

    if (remove(path.c_str()) < 0) {
        print_error("Failed to remove file or directory");
    }
}

void cmd_cat(const CommandContext& ctx) {
    if (ctx.args.empty()) {
        print_error("No file specified");
        return;
    }

    const char* path = ctx.args[0].c_str();
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        print_error("Failed to open file");
        return;
    }

    char buffer[1024];
    ssize_t bytesRead;
    while ((bytesRead = read(fd, buffer, sizeof(buffer))) > 0) {
        shell_print(std::string(buffer, bytesRead));
    }
    shell_print("\n");

    close(fd);
}

void cmd_kilo(const CommandContext& ctx) {
    if (ctx.args.empty()) {
        print_error("kilo: missing file");
        return;
    }

    kilo_editor(ctx, ctx.args[0].c_str());
}

void kilo_editor(const CommandContext& ctx, const char* filename) {
    (void)ctx;

    pid_t pid = fork();

    if (pid == -1) {
        perror("fork failed");
        return;
    }

    if (pid == 0) {
        int tty = open("/dev/tty1", O_RDWR);

        if (tty >= 0) {
            dup2(tty, 0);
            dup2(tty, 1);
            dup2(tty, 2);

            if (tty > 2) {
                close(tty);
            }
        }

        setenv("TERM", "linux", 1);

        const char* path = "/bin/kilo";
        char* const args[] = {
            (char*)path,
            (char*)filename,
            NULL
        };

        execv(path, args);

        perror("execv failed");
        _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, 0);

    shell_print("\033[2J\033[H");

    if (WIFEXITED(status)) {
        shell_print("kilo exited with code: " + std::to_string(WEXITSTATUS(status)) + "\n");
    } else if (WIFSIGNALED(status)) {
        shell_print("kilo killed by signal: " + std::to_string(WTERMSIG(status)) + "\n");
    } else {
        shell_print("kilo returned with unknown status\n");
    }

    shell_print("Returned to DewOS Init.\n");
}

void cmd_dewfetch(const CommandContext& ctx) {
    (void)ctx;
    struct utsname sys;
    uname(&sys);

    std::string uptime_str = "unknown";
    std::ifstream uf("/proc/uptime");
    if (uf.is_open()) {
        std::string raw;
        if (uf >> raw) {
            size_t dot = raw.find('.');
            if (dot != std::string::npos) raw = raw.substr(0, dot);
            try {
                long long sec = std::stoll(raw);
                uptime_str = std::to_string(sec / 3600) + "h " + std::to_string((sec % 3600) / 60) + "m";
            } catch (...) {}
        }
        uf.close();
    }

    std::string mem_str = "unknown";
    std::ifstream mf("/proc/meminfo");
    if (mf.is_open()) {
        long total = 0, avail = 0;
        std::string line;
        while (std::getline(mf, line)) {
            if (line.find("MemTotal:") == 0) {
                sscanf(line.c_str(), "MemTotal: %ld", &total);
            } else if (line.find("MemAvailable:") == 0 || line.find("MemFree:") == 0) {
                if (avail == 0) sscanf(line.c_str(), line.find("MemAvailable:") == 0 ? "MemAvailable: %ld" : "MemFree: %ld", &avail);
            }
        }
        if (total > 0) {
            mem_str = std::to_string((total - avail) / 1024) + "MiB / " + std::to_string(total / 1024) + "MiB";
        }
        mf.close();
    }

    int pkg_count = 0;
    DIR* drop_dir = opendir("/var/drop/installed");
    if (drop_dir) {
        while (struct dirent* e = readdir(drop_dir)) {
            std::string n = e->d_name;
            if (n == "." || n == "..") continue;
            // skip .meta files — count only main entries
            if (n.size() >= 5 && n.substr(n.size() - 5) == ".meta") continue;
            pkg_count++;
        }
        closedir(drop_dir);
    }

    std::vector<std::string> logo = {
        std::string(LIGHT_BLUE) + "          .::--==++**░▒▓▓▓▓▓▓▒░:.              " + RESET,
        std::string(LIGHT_BLUE) + "             .:-=+*░▒▓" + BLUE + "████████████▄           " + RESET,
        std::string(LIGHT_BLUE) + "                .-=*▒▓" + BLUE + "██████████████▄         " + RESET,
        std::string(LIGHT_BLUE) + "                    .:+▓" + BLUE + "████████████▓▓       " + RESET,
        std::string(BLUE)       + "                               ▐████████▌      " + RESET,
        std::string(BLUE)       + "                                  ▐████████▌    " + RESET,
        std::string(BLUE)       + "                                    ▐███████▌   " + RESET,
        std::string(BLUE)       + "                                      ▐██████▌  " + RESET,
        std::string(BLUE)       + "                                    ▄███████▀   " + RESET,
        std::string(BLUE)       + "                                  ▄████████▀    " + RESET,
        std::string(BLUE)       + "                                ▄████████▀      " + RESET,
        std::string(LIGHT_BLUE) + "                    .:+▓" + BLUE + "██████████████▀       " + RESET,
        std::string(LIGHT_BLUE) + "                .-=*▒▓" + BLUE + "██████████████▀         " + RESET,
        std::string(LIGHT_BLUE) + "             .:-=+*░▒▓" + BLUE + "████████████▀           " + RESET,
        std::string(LIGHT_BLUE) + "          .::--==++**░▒▓▓▓▓▓▒░:.              " + RESET
    };

    std::string host = sys.nodename;
    if (host == "(none)" || host.empty()) host = "dewos";

    shell_print("\n");
    for (size_t i = 0; i < logo.size(); ++i) {
        shell_print("  " + logo[i]);

        switch(i) {
            case 1:  shell_print("  " + std::string(BOLD) + BLUE + "dewos" + RESET + "@" + BOLD + BLUE + host); break;
            case 2:  shell_print("  ------------"); break;
            case 3:  shell_print("   " + std::string(BOLD) + BLUE + "os     " + RESET + "DewOS"); break;
            case 4:  shell_print("  " + std::string(BOLD) + BLUE + "kernel " + RESET + sys.release); break;
            case 5:  shell_print("  " + std::string(BOLD) + BLUE + "uptime " + RESET + uptime_str); break;
            case 6:  shell_print("  " + std::string(BOLD) + BLUE + "pkgs   " + RESET + std::to_string(pkg_count) + " (drop)"); break;
            case 7:  shell_print("  " + std::string(BOLD) + BLUE + "memory " + RESET + mem_str); break;
            case 9:  shell_print("  " + std::string("\033[41m  \033[42m  \033[43m  \033[44m  \033[45m  \033[0m")); break;
        }

        shell_print("\n");
    }

    shell_print("\n");
}

void cmd_ps(const CommandContext& ctx) {
    (void)ctx;

    DIR* dir = opendir("/proc");
    if (!dir) return;

    long page_size_kb = sysconf(_SC_PAGESIZE) / 1024;
    auto cell = [](std::string text, size_t width) {
        if (text.size() > width) {
            return text.substr(0, width);
        }

        text.append(width - text.size(), ' ');
        return text;
    };

    shell_print(std::string(BOLD) + CYN + "╭──────────┬──────────────────────┬────────────┬──────────────────────╮" + RESET + "\n");
    shell_print(std::string(BOLD) + CYN + "│" + RESET + BOLD + "  PID     " + CYN + "│" + RESET + BOLD + "  COMMAND             " + CYN + "│" + RESET + BOLD + "  RAM       " + CYN + "│" + RESET + BOLD + "  STATUS              " + CYN + "│" + RESET + "\n");
    shell_print(std::string(BOLD) + CYN + "├──────────┼──────────────────────┼────────────┼──────────────────────┤" + RESET + "\n");

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_DIR && isdigit(entry->d_name[0])) {
            std::string pid = entry->d_name;
            std::string path = "/proc/" + pid;

            std::ifstream comm_file(path + "/comm");
            std::string command;
            std::getline(comm_file, command);
            if (command.length() > 18) command = command.substr(0, 15) + "...";
            if (command.empty()) command = "kernel_task";

            std::ifstream stat_file(path + "/stat");
            std::string dummy, state_code;
            stat_file >> dummy >> dummy >> state_code;

            std::string state_display;
            if (state_code == "R")      state_display = std::string(GNR) + "󰐊 Run" + RESET;
            else if (state_code == "S") state_display = std::string(CYN) + "󰒲 Slp" + RESET;
            else if (state_code == "Z") state_display = std::string(RED) + "󰚑 Zmb" + RESET;
            else                        state_display = state_code;

            std::ifstream statm_file(path + "/statm");
            long pages;
            statm_file >> pages;
            std::string ram_usage = std::to_string(pages * page_size_kb) + "K";

            std::stringstream ss;
            ss << std::string(BOLD) << CYN << "│ " << RESET << cell(pid, 9)
            << BOLD << CYN << "│ " << RESET << cell(command, 21)
            << BOLD << CYN << "│ " << RESET << cell(ram_usage, 11)
            << BOLD << CYN << "│ " << RESET << cell(state_display, 30)
            << BOLD << CYN << "│" << RESET << "\n";

            shell_print(ss.str());
        }
    }

    shell_print(std::string(BOLD) + CYN + "╰──────────┴──────────────────────┴────────────┴──────────────────────╯" + RESET + "\n");
    closedir(dir);
}

void enable_raw_mode() {
    if (tcgetattr(0, &original_termios) < 0) {
        print_error("tcgetattr failed");
        return;
    }

    termios raw = original_termios;

    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(0, TCSAFLUSH, &raw) < 0) {
        print_error("tcsetattr failed");
    }
}


void disable_raw_mode() {
    tcsetattr(0, TCSAFLUSH, &original_termios);
}


std::string read_line(const std::string& prompt) {
    std::string line;
    size_t cursor = 0;

    bool cursor_visible = true;

    history_index = history.size();

    redraw_line(prompt, line, cursor, cursor_visible);

    while (true) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(0, &readfds);

        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 250000; 

        int ready = select(1, &readfds, nullptr, nullptr, &timeout);

        if (ready == 0) {
            cursor_visible = !cursor_visible;
            redraw_line(prompt, line, cursor, cursor_visible);
            continue; 
        }

        if (ready < 0) {
            continue;
        }

        char c;
        if (read(0, &c, 1) != 1) {
            continue;
        }

        if (c == '\n' || c == '\r') {
            redraw_line(prompt, line, cursor, false);
            raw_write("\n");

            if (!line.empty()) {
                history.push_back(line);
            }
            return line;
        }

        if (c == 127 || c == '\b') {
            if (cursor > 0) {
                line.erase(line.begin() + cursor - 1);
                cursor--;
                redraw_line(prompt, line, cursor, cursor_visible);
            }
            continue;
        }

        if (c == '\033') {
            char seq[3];

            if (read(0, &seq[0], 1) != 1) {
                continue;
            }

            if (read(0, &seq[1], 1) != 1) {
                continue;
            }

            if (seq[0] == '[') {
                if (seq[1] == 'A') {
                    if (!history.empty() && history_index > 0) {
                        history_index--;
                        line = history[history_index];
                        cursor = line.size();
                        redraw_line(prompt, line, cursor, cursor_visible);
                    }
                } else if (seq[1] == 'B') {
                    if (history_index + 1 < history.size()) {
                        history_index++;
                        line = history[history_index];
                    } else {
                        history_index = history.size();
                        line.clear();
                    }

                    cursor = line.size();
                    redraw_line(prompt, line, cursor, cursor_visible);
                } else if (seq[1] == 'C') {
                    if (cursor < line.size()) {
                        cursor++;
                        redraw_line(prompt, line, cursor, cursor_visible);
                    }
                } else if (seq[1] == 'D') {
                    if (cursor > 0) {
                        cursor--;
                        redraw_line(prompt, line, cursor, cursor_visible);
                    }
                }
            }

            continue;
        }

        if (c >= 32 && c <= 126) {
            line.insert(line.begin() + cursor, c);
            cursor++;
            redraw_line(prompt, line, cursor, cursor_visible);
        }
    }
}

void redraw_line(const std::string& prompt, const std::string& line, size_t cursor, bool cursor_visible) {
    if (cursor > line.size()) {
        cursor = line.size();
    }

    raw_write("\r");
    raw_write("\033[K");
    raw_write(prompt);

    std::string before = line.substr(0, cursor);
    std::string after = line.substr(cursor);

    raw_write(before);

    if (cursor_visible) {
        raw_write("|");
    } else {
        raw_write(" ");
    }

    raw_write(after);
    raw_write("\033[K");

    size_t chars_right = after.size();

    if (chars_right > 0) {
        raw_write("\033[" + std::to_string(chars_right) + "D");
    }
}

void raw_write(const std::string& text) {
    const char* data = text.data();
    size_t left = text.size();

    while (left > 0) {
        ssize_t written = write(STDOUT_FILENO, data, left);

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }

            return;
        }

        data += written;
        left -= static_cast<size_t>(written);
    }
}

void shell_print(const std::string& text) {
    for (char c : text) {
        if (shell_output.escape_sequence) {
            if (c >= '@' && c <= '~') {
                shell_output.escape_sequence = false;
            }
            continue;
        }

        if (c == '\033') {
            shell_output.escape_sequence = true;
        } else if (c == '\n') {
            shell_output.row++;
            shell_output.col = 0;
        } else if (c == '\r') {
            shell_output.col = 0;
        } else if (c == '\b') {
            if (shell_output.col > 0) {
                shell_output.col--;
            }
        } else if (static_cast<unsigned char>(c) >= 32) {
            shell_output.col++;
        }
    }

    raw_write(text);
}

bool detect_rootfs() {
    return access("/etc/dewos-installed", F_OK) == 0;
}

bool require_rootfs(const std::string& command_name) {
    if (rootfs_ready) {
        return true;
    }

    shell_print(command_name + ": unavailable before install/rootfs mount\n");
    return false;
}

void cmd_sh(const CommandContext& ctx) {
    (void)ctx;

    shell_print("[DewOS] Starting /bin/sh...\n");

    disable_raw_mode();
    raw_write("\033[?25h");

    pid_t pid = fork();

    if (pid < 0) {
        shell_print("[DewOS] fork failed\n");
        enable_raw_mode();
        raw_write("\033[?25l");
        return;
    }

    if (pid == 0) {
        const char* path = "/bin/sh";
        char* const args[] = {
            (char*)path,
            NULL
        };

        execv(path, args);

        perror("execv /bin/sh failed");
        _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, 0);

    enable_raw_mode();
    raw_write("\033[?25l");

    shell_print("\n[DewOS] Returned from shell\n");
}

void start_network_if_installed() {
    if (!rootfs_ready) {
        return;
    }

    std::string network_type = "ethernet";
    std::string iface = "eth0";
    std::ifstream config("/etc/dewos/network.conf");

    if (config.is_open()) {
        std::string line;

        while (std::getline(config, line)) {
            size_t eq = line.find('=');

            if (eq == std::string::npos) {
                continue;
            }

            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);

            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }

            if (key == "NETWORK_TYPE") {
                network_type = value;
            } else if (key == "NETWORK_IFACE") {
                iface = value;
            }
        }
    }

    if (network_type == "skip" || iface.empty()) {
        return;
    }

    const char* path = network_type == "wifi" ? "/sbin/wifi-up" : "/sbin/netup";

    if (access(path, X_OK) != 0) {
        return;
    }

    pid_t pid = fork();

    if (pid < 0) {
        return;
    }

    if (pid == 0) {
        int log = open("/tmp/netup.log", O_CREAT | O_WRONLY | O_TRUNC, 0644);

        if (log >= 0) {
            dup2(log, 1);
            dup2(log, 2);

            if (log > 2) {
                close(log);
            }
        }

        char* const args[] = {
            (char*)path,
            (char*)iface.c_str(),
            nullptr
        };

        execv(path, args);
        _exit(127);
    }
}

void cmd_netlog(const CommandContext& ctx) {
    (void)ctx;

    int fd = open("/tmp/netup.log", O_RDONLY);

    if (fd < 0) {
        shell_print("netlog: no network log found\n");
        return;
    }

    char buffer[1024];
    ssize_t n;

    while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
        shell_print(std::string(buffer, n));
    }

    close(fd);
}

void cmd_drop(const CommandContext& ctx) {
    disable_raw_mode();
    raw_write("\033[?25h");

    pid_t pid = fork();

    if (pid < 0) {
        shell_print("[DewOS] fork failed\n");
        enable_raw_mode();
        raw_write("\033[?25l");
        return;
    }

    if (pid == 0) {
        std::vector<std::string> argv = {"drop"};
        for (const auto& f : ctx.flags) argv.push_back(f);
        for (const auto& a : ctx.args) argv.push_back(a);

        std::vector<char*> cargs;
        for (auto& s : argv) cargs.push_back(const_cast<char*>(s.c_str()));
        cargs.push_back(nullptr);

        execv("/bin/drop", cargs.data());
        perror("execv /bin/drop failed");
        _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, 0);

    enable_raw_mode();
    raw_write("\033[?25l");
}

void unknown_command(const CommandContext& ctx) {
    shell_print("Unknown command: " + ctx.name + "\n");
}
