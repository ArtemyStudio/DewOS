#include "../include/init.hpp"
#include "../include/system.hpp"
#include "../include/shell.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdio>

#include <unistd.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <linux/reboot.h>
#include <termios.h>

int current_uid = 0;
int current_gid = 0;

std::string current_user = "root";
std::string current_home = "/root";

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

    std::string password = read_password("password: ");

    if (!load_user_from_files(username, user) || !verify_password(password, user.password_hash)) {
        std::cout << "Login or password incorrect\n";
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
    (void)password; (void)hash;
    return true;
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
    char cwd[1024];

    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        return "[dewos@" + current_user + " " + std::string(cwd) + "]# ";
    } else {
        return "[dewos@" + current_user + "]# ";
    }
}

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

void cmd_id(const CommandContext& ctx) {
    (void)ctx;

    shell_print("uid=" + std::to_string(current_uid));
    shell_print("(" + current_user + ") ");
    shell_print("gid=" + std::to_string(current_gid) + "\n");
}


void cmd_whoami(const CommandContext& ctx) {
    (void)ctx;

    shell_print(current_user + "\n");
}

void cmd_mount(const CommandContext& ctx) {
    if (ctx.args.empty()) {
        print_error("usage: mount <device> [mount_point]");
        return;
    }

    if (ctx.args[0] == "-r" || ctx.args[0] == "-t" || ctx.args[0] == "-o") {
        print_error("mount: flag not supported in built-in; use /bin/sh and run mount directly");
        return;
    }

    std::string device = ctx.args[0];
    std::string mount_point = (ctx.args.size() >= 2)
        ? ctx.args[1]
        : ("/mnt/" + device.substr(device.find_last_of('/') + 1));

    mkdir("/mnt", 0755);
    mkdir(mount_point.c_str(), 0755);

    disable_raw_mode();
    raw_write("\033[?25h");

    pid_t pid = fork();
    if (pid < 0) {
        print_error("mount: fork failed");
        enable_raw_mode();
        raw_write("\033[?25l");
        return;
    }
    if (pid == 0) {
        const char* candidates[] = {"/bin/mount", "/sbin/mount", nullptr};
        for (int i = 0; candidates[i]; ++i) {
            char* const args[] = {
                (char*)candidates[i],
                (char*)device.c_str(),
                (char*)mount_point.c_str(),
                nullptr
            };
            execv(candidates[i], args);
        }
        perror("mount: execv failed");
        _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, 0);

    enable_raw_mode();
    raw_write("\033[?25l");

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        shell_print("mount: " + device + " -> " + mount_point + "\n");
    } else {
        shell_print("mount: failed\n");
    }
}

void cmd_umount(const CommandContext& ctx) {
    if (ctx.args.empty()) {
        print_error("usage: umount <mount_point>");
        return;
    }

    std::string target = ctx.args[0];
    if (target.find('/') == std::string::npos) {
        target = "/mnt/" + target;
    }

    disable_raw_mode();
    raw_write("\033[?25h");

    pid_t pid = fork();
    if (pid < 0) {
        print_error("umount: fork failed");
        enable_raw_mode();
        raw_write("\033[?25l");
        return;
    }
    if (pid == 0) {
        const char* candidates[] = {"/bin/umount", "/sbin/umount", nullptr};
        for (int i = 0; candidates[i]; ++i) {
            char* const args[] = {
                (char*)candidates[i],
                (char*)target.c_str(),
                nullptr
            };
            execv(candidates[i], args);
        }
        perror("umount: execv failed");
        _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, 0);

    enable_raw_mode();
    raw_write("\033[?25l");

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        shell_print("umount: " + target + " unmounted\n");
    } else {
        shell_print("umount: failed\n");
    }
}

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
