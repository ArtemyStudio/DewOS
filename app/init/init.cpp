#include "init.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdint>
#include <iomanip>
#include <vector>
#include <cerrno>
#include <cstdio>
#include <cstddef>

#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <crypt.h>
#include <sys/wait.h>
#include <sys/reboot.h>
#include <linux/reboot.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/utsname.h>
#include <sys/sysmacros.h>

int main() {
    setup_console();
    mount_basic_fs();

    rootfs_ready = detect_rootfs();

    if (rootfs_ready) {
        cmd_clear(CommandContext{});
        print_banner();

        start_network_if_installed();

        login_loop();
    } else {
        current_user = "root";
        current_uid = 0;
        current_gid = 0;
        current_home = "/root";
    }

    enable_raw_mode();
    raw_write("\033[?25l");

    cmd_clear(CommandContext{});
    print_banner();

    shell_loop();

    return 0;
}

//-------------------------------------------------------------------------------------//

void setup_console() {
    mkdir("/dev", 0755);
    
    if (mknod("/dev/tty1", S_IFCHR | 0600, makedev(4, 1)) < 0) {
        if (errno != EEXIST) {
            critical_error("DEVTMPFS_MKNOD_TTY1_FAILED", 0xDE771001);
        }
    }

    if (mknod("/dev/console", S_IFCHR | 0666, makedev(5, 1)) < 0) {
        if (errno != EEXIST) {
            critical_error("DEVTMPFS_MKNOD_CONSOLE_FAILED", 0xDE771002);
        }
    }

    int tty = open("/dev/tty1", O_RDWR);

    if (tty < 0) {
        tty = open("/dev/console", O_RDWR);
    }

    if (tty < 0) {
        critical_error("TTY_INIT_FAILURE", 0xDE171CE0);
    }

    dup2(tty, 0);
    dup2(tty, 1);
    dup2(tty, 2);

    if (tty > 2) {
        close(tty);
    }
}

void mount_basic_fs() {
    mkdir("/proc", 0555);
    mkdir("/sys", 0555);
    mkdir("/tmp", 01777);

    if (mount("proc", "/proc", "proc", 0, nullptr) < 0 && errno != EBUSY) {
        print_error("mount: failed to mount /proc");
    }

    if (mount("sysfs", "/sys", "sysfs", 0, nullptr) < 0 && errno != EBUSY) {
        print_error("mount: failed to mount /sys");
    }
}

//-------------------------------------------------------------------------------------//

void print_error(const std::string& message) {
    std::cerr << "ERROR: " << message << std::endl;
}

void shell_loop() {
    std::vector<Command> commands = create_commands();

    while (true) {
        std::string input = read_line(make_prompt());

        scroll_offset = 0;

        CommandContext ctx = parse_command(input);

        if (ctx.name == "help") {
            cmd_help(ctx, commands);
            continue;
        }

        exec_command(ctx, commands);
    }
}

void emergency_print(const char* str, int x, int y, uint8_t color) {
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    int offset = y * 80 + x;

    for (int i = 0; str[i] != '\0'; ++i) {
        vga[offset + i] = (uint16_t)str[i] | ((uint16_t)color << 8);
    }
}

void hex_to_str(uint32_t n, char* out) {
    const char* hex_chars = "0123456789ABCDEF";
    
    out[0] = '0';
    out[1] = 'x';
    
    for (int i = 7; i >= 0; --i) {
        out[i + 2] = hex_chars[n & 0xF];
        n >>= 4;
    }
    
    out[10] = '\0';
}

void critical_error(const char* message, uint32_t error_code) {
    uint16_t* vga = (uint16_t*)0xB8000;
    uint8_t color = 0x1F; 

    for (int i = 0; i < 80 * 25; i++) {
        vga[i] = (uint16_t)' ' | (uint16_t)(color << 8);
    }

    const char* logo[] = {
        " ██████╗██████╗ ██╗████████╗██╗ ██████╗ █████╗ ██╗      ",
        "██╔════╝██╔══██╗██║╚══██╔══╝██║██╔════╝██╔══██╗██║      ",
        "██║     ██████╔╝██║   ██║   ██║██║     ███████║██║      ",
        "██║     ██╔══██╗██║   ██║   ██║██║     ██╔══██║██║      ",
        "╚██████╗██║  ██║██║   ██║   ██║╚██████╗██║  ██║███████╗ ",
        " ╚═════╝╚═╝  ╚═╝╚═╝   ╚═╝   ╚═╝ ╚═════╝╚═╝  ╚═╝╚══════╝ ",
        "                                                        ",
        "███████╗██████╗ ██████╗  ██████╗ ██████╗                ",
        "██╔════╝██╔══██╗██╔══██╗██╔═══██╗██╔══██╗               ",
        "█████╗  ██████╔╝██████╔╝██║   ██║██████╔╝               ",
        "██╔══╝  ██╔══██╗██╔══██╗██║   ██║██╔══██╗               ",
        "███████╗██║  ██║██║  ██║╚██████╔╝██║  ██║               ",
        "╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝ ╚═╝  ╚═╝               "
    };

    for (int i = 0; i < 13; i++) {
        emergency_print(logo[i], 10, 2 + i, color);
    }

    emergency_print("----------------------------------------------------------", 10, 16, color);
    
    emergency_print("KERNEL PANIC:", 10, 17, color);
    emergency_print(message, 24, 17, 0x1E);

    char hex_buf[11];
    hex_to_str(error_code, hex_buf);
    emergency_print("ERROR CODE: ", 10, 18, color);
    emergency_print(hex_buf, 22, 18, 0x1F);

    emergency_print("SYSTEM HALTED. Please manually restart your computer.", 10, 20, color);

    while (true) {
        __asm__ __volatile__ ("hlt");
    }
}

//-------------------------------------------------------------------------------------//

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
    commands.push_back(Command{"umount", "Unmount path", cmd_umount});
    commands.push_back(Command{"su", "Switch user", cmd_su});
    commands.push_back(Command{"netlog", "Show network startup log", cmd_netlog});

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

    cmd_unknown(ctx);
}

//-------------------------------------------------------------------------------------//

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

void cmd_whoami(const CommandContext& ctx) {
    (void)ctx;

    shell_print(current_user + "\n");
}

void cmd_id(const CommandContext& ctx) {
    (void)ctx;

    shell_print("uid=" + std::to_string(current_uid));
    shell_print("(" + current_user + ") ");
    shell_print("gid=" + std::to_string(current_gid) + "\n");
}

void cmd_logout(const CommandContext& ctx) {
    (void)ctx;

    disable_raw_mode();
    raw_write("\033[?25h");
    cmd_clear(CommandContext{});

    login_loop();

    enable_raw_mode();
    raw_write("\033[?25l");

    cmd_clear(CommandContext{});
    print_banner();
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

void cmd_umount(const CommandContext& ctx) {
    (void)ctx;

    if (ctx.args.empty()) {
        shell_print("usage: umount <path>\n");
        return;
    }

    pid_t pid = fork();

    if (pid < 0) {
        shell_print("umount: fork failed\n");
        return;
    }

    if (pid == 0) {
        const char* target = ctx.args[0].c_str();

        {
            const char* path = "/bin/umount";
            char* const args[] = {
                (char*)path,
                (char*)target,
                nullptr
            };

            execv(path, args);
        }

        {
            const char* path = "/sbin/umount";
            char* const args[] = {
                (char*)path,
                (char*)target,
                nullptr
            };

            execv(path, args);
        }

        perror("execv umount failed");
        _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, 0);
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

//-------------------------------------------------------------------------------------//

void sys_poweroff(const CommandContext& ctx) {
    (void)ctx;
    shell_print("Powering off...\n");

    sync();
    reboot(LINUX_REBOOT_CMD_POWER_OFF);
}

void sys_reboot(const CommandContext& ctx) {
    (void)ctx;
    shell_print("Rebooting...\n");

    sync();
    reboot(LINUX_REBOOT_CMD_RESTART);
}

//-------------------------------------------------------------------------------------//

void cmd_pwd(const CommandContext& ctx) {
    (void)ctx;

    if (!require_rootfs("pwd")) return;

    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        shell_print(std::string(cwd) + "\n");
    } else {
        print_error("Failed to get current working directory");
    }
}

void cmd_ls(const CommandContext& ctx) {
    (void)ctx;

    if (!require_rootfs("ls")) return;

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
    if (!require_rootfs("cd")) return;

    if (ctx.args.empty()) {
        print_error("No directory specified");
        return;
    }

    if (chdir(ctx.args[0].c_str()) < 0) {
        print_error("Failed to change directory");
    }
}

void cmd_mkdir(const CommandContext& ctx) {
    if (!require_rootfs("mkdir")) return;

    if (ctx.args.empty()) {
        print_error("No directory name specified");
        return;
    }

    if (mkdir(ctx.args[0].c_str(), 0755) < 0) {
        print_error("Failed to create directory");
    }
}

void cmd_touch(const CommandContext& ctx) {
    if (!require_rootfs("touch")) return;

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
    if (!require_rootfs("rm")) return;

    if (ctx.args.empty()) {
        print_error("No file or directory specified");
        return;
    }

    const char* path = ctx.args[0].c_str();

    if (ctx.has_flag("-r")) {
        shell_print("Recursively removing directory is not implemented yet.\n");
    } else {
        if (remove(path) < 0) {
            print_error("Failed to remove file or directory");
        }
    }
}

void cmd_cat(const CommandContext& ctx) {
    if (!require_rootfs("cat")) return;

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

//-------------------------------------------------------------------------------------//

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

//-------------------------------------------------------------------------------------//

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
    DIR* dir = opendir("/usr/bin");
    if (dir) {
        while (readdir(dir)) pkg_count++;
        closedir(dir);
        if (pkg_count > 2) pkg_count -= 2;
    }

    std::vector<std::string> logo = {
        std::string(LIGHT_BLUE) + "          .::--==++**░▒▓▓▓▓▓▓▒░:.              " + RESET,
        std::string(LIGHT_BLUE) + "             .:-=+*░▒▓" + BLUE + "████████████▄           " + RESET,
        std::string(LIGHT_BLUE) + "                .-=*▒▓" + BLUE + "██████████████▄         " + RESET,
        std::string(LIGHT_BLUE) + "                    .:+▓" + BLUE + "████████████▓▓       " + RESET,
        std::string(BLUE)       + "                              ▐████████▌      " + RESET,
        std::string(BLUE)       + "                                ▐████████▌    " + RESET,
        std::string(BLUE)       + "                                  ▐███████▌   " + RESET,
        std::string(BLUE)       + "                                    ▐██████▌  " + RESET,
        std::string(BLUE)       + "                                  ▄███████▀   " + RESET,
        std::string(BLUE)       + "                                ▄████████▀    " + RESET,
        std::string(BLUE)       + "                              ▄████████▀      " + RESET,
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
            case 6:  shell_print("  " + std::string(BOLD) + BLUE + "pkgs   " + RESET + std::to_string(pkg_count) + " (bin)"); break;
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
            ss << std::string(BOLD) << CYN << "│ " << RESET << std::left << std::setw(9) << pid
            << BOLD << CYN << "│ " << RESET << std::left << std::setw(21) << command
            << BOLD << CYN << "│ " << RESET << std::left << std::setw(11) << ram_usage
            << BOLD << CYN << "│ " << RESET << std::left << std::setw(30) << state_display
            << BOLD << CYN << "│" << RESET << "\n";

            shell_print(ss.str());
        }
    }

    shell_print(std::string(BOLD) + CYN + "╰──────────┴──────────────────────┴────────────┴──────────────────────╯" + RESET + "\n");
    closedir(dir);
}

//-------------------------------------------------------------------------------------//

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
                }
                else if (seq[1] == 'B') {
                    if (history_index + 1 < history.size()) {
                        history_index++;
                        line = history[history_index];
                    } else {
                        history_index = history.size();
                        line.clear();
                    }

                    cursor = line.size();
                    redraw_line(prompt, line, cursor, cursor_visible);
                }
                else if (seq[1] == 'C') {
                    if (cursor < line.size()) {
                        cursor++;
                        redraw_line(prompt, line, cursor, cursor_visible);
                    }
                }
                else if (seq[1] == 'D') {
                    if (cursor > 0) {
                        cursor--;
                        redraw_line(prompt, line, cursor, cursor_visible);
                    }
                }
                else if (seq[1] == '5' || seq[1] == '6') {
                    char tilde;
                    if (read(0, &tilde, 1) != 1) {
                        continue;
                    }

                    if (tilde == '~') {
                        if (seq[1] == '5') {
                            scroll_up();
                        } else {
                            scroll_down();
                        }
                    }
                }
            }

            continue;
        }

        if (c >= 32 && c <= 126) {
            scroll_offset = 0; 
            line.insert(line.begin() + cursor, c);
            cursor++;
            redraw_line(prompt, line, cursor, cursor_visible);
        }
    }
}   

void raw_write(const std::string& text) {
    (void)!write(STDOUT_FILENO, text.c_str(), text.size());
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

//-------------------------------------------------------------------------------------//

void scroll_up() {
    int total = static_cast<int>(scrollback.size());

    if (total <= SCREEN_ROWS) {
        return;
    }

    scroll_offset += 5;

    int max_offset = total - SCREEN_ROWS;
    if (scroll_offset > max_offset) {
        scroll_offset = max_offset;
    }

    redraw_screen_scroll();
}

void scroll_down() {
    scroll_offset -= 5;

    if (scroll_offset < 0) {
        scroll_offset = 0;
    }

    redraw_screen_scroll();
}

void redraw_screen_scroll() {
    raw_write("\033[2J\033[H");

    int total = static_cast<int>(scrollback.size());
    int visible = SCREEN_ROWS - 2;

    int start = total - visible - scroll_offset;
    if (start < 0) {
        start = 0;
    }

    int end = start + visible;
    if (end > total) {
        end = total;
    }

    for (int i = start; i < end; i++) {
        raw_write(scrollback[i] + "\n");
    }
}

void shell_print(const std::string& text) {
    raw_write(text);

    static std::string current_line;

    for (char c : text) {
        if (c == '\r') {
            continue;
        }

        if (c == '\033') {}

        if (c == '\n') {
            if (!current_line.empty()) {
                scrollback.push_back(current_line);
                current_line.clear();
            }
        } else {
            current_line.push_back(c);
        }
    }

    if (scrollback.size() > 1000) {
        scrollback.erase(scrollback.begin(), scrollback.begin() + 100);
    }
}

//------------------------------------------------------------------------------------//

void login_loop() {
    while (true) {
        if (login_once()) {
            return;
        }
    }
}

bool login_once() {
    std::string username;

    std::cout << "login: " << std::flush;
    std::getline(std::cin, username);

    if (username.empty()) {
        return false;
    }

    LoginUser user;

    if (!load_user_from_files(username, user)) {
        std::cout << "Login incorrect\n";
        return false;
    }

    std::string password = read_password("password: ");

    if (!verify_password(password, user.password_hash)) {
        std::cout << "Login incorrect\n";
        return false;
    }

    current_user = user.name;
    current_uid = user.uid;
    current_gid = user.gid;
    current_home = user.home;

    if (!current_home.empty()) {
        if (chdir(current_home.c_str()) != 0) {
            print_error("login: failed to change home directory");
        }
    }

    std::cout << "Welcome to DewOS, " << current_user << "\n";
    return true;
}

bool load_user_from_files(const std::string& username, LoginUser& user) {
    std::ifstream passwd_file("/etc/passwd");

    if (!passwd_file.is_open()) {
        print_error("login: cannot open /etc/passwd");
        return false;
    }

    std::string line;
    bool found_passwd = false;

    while (std::getline(passwd_file, line)) {
        std::vector<std::string> parts = split_string(line, ':');

        if (parts.size() < 7) {
            continue;
        }

        if (parts[0] == username) {
            user.name = parts[0];
            user.uid = std::stoi(parts[2]);
            user.gid = std::stoi(parts[3]);
            user.home = parts[5];
            user.shell = parts[6];

            found_passwd = true;
            break;
        }
    }

    passwd_file.close();

    if (!found_passwd) {
        return false;
    }

    std::ifstream shadow_file("/etc/shadow");

    if (!shadow_file.is_open()) {
        print_error("login: cannot open /etc/shadow");
        return false;
    }

    bool found_shadow = false;

    while (std::getline(shadow_file, line)) {
        std::vector<std::string> parts = split_string(line, ':');

        if (parts.size() < 2) {
            continue;
        }

        if (parts[0] == username) {
            user.password_hash = parts[1];
            found_shadow = true;
            break;
        }
    }

    shadow_file.close();

    return found_shadow;
}

std::string read_password(const std::string& prompt) {
    std::cout << prompt << std::flush;

    termios old_termios;
    termios new_termios;

    if (tcgetattr(STDIN_FILENO, &old_termios) < 0) {
        std::string fallback;
        std::getline(std::cin, fallback);
        return fallback;
    }

    new_termios = old_termios;
    new_termios.c_lflag &= ~ECHO;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_termios);

    std::string password;
    std::getline(std::cin, password);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_termios);

    std::cout << "\n";
    return password;
}

bool verify_password(const std::string& password, const std::string& hash) {
    if (hash.empty() || hash == "*" || hash == "!") {
        return false;
    }

    char* result = crypt(password.c_str(), hash.c_str());

    if (result == nullptr) {
        return false;
    }

    return hash == result;
}

std::vector<std::string> split_string(const std::string& text, char sep) {
    std::vector<std::string> parts;
    std::string current;

    for (char c : text) {
        if (c == sep) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }

    parts.push_back(current);
    return parts;
}

std::string make_prompt() {
    return "[dewos@" + current_user + "]# ";
}

//------------------------------------------------------------------------------------//

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

//------------------------------------------------------------------------------------//

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

    shell_print("\n[DewOS] Returned from shell.\n");
}

//------------------------------------------------------------------------------------//

void cmd_su(const CommandContext& ctx) {
    if (!rootfs_ready) {
        shell_print("su: unavailable in live installer mode\n");
        return;
    }

    if (ctx.args.empty()) {
        shell_print("usage: su <user>\n");
        return;
    }

    std::string username = ctx.args[0];

    LoginUser user;

    if (!load_user_from_files(username, user)) {
        shell_print("su: user not found\n");
        return;
    }

    disable_raw_mode();
    raw_write("\033[?25h");

    std::string password = read_password("password: ");

    enable_raw_mode();
    raw_write("\033[?25l");

    if (!verify_password(password, user.password_hash)) {
        shell_print("su: authentication failed\n");
        return;
    }

    current_user = user.name;
    current_uid = user.uid;
    current_gid = user.gid;
    current_home = user.home;

    if (!current_home.empty()) {
        if (chdir(current_home.c_str()) != 0) {
            print_error("su: failed to change home directory");
        }
    }

    shell_print("switched to " + current_user + "\n");
}

//------------------------------------------------------------------------------------//

void start_network_if_installed() {
    if (!rootfs_ready) {
        return;
    }

    if (access("/sbin/netup", X_OK) != 0) {
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

        const char* path = "/sbin/netup";

        char* const args[] = {
            (char*)path,
            (char*)"eth0",
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

//------------------------------------------------------------------------------------//

void cmd_unknown(const CommandContext& ctx) {
    shell_print("Unknown command: " + ctx.name + "\n");
}