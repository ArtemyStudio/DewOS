#pragma once

#include "shell.hpp"

#include <string>
#include <vector>

struct LoginUser {
    std::string name;
    int uid;
    int gid;
    std::string home;
    std::string shell;
    std::string password_hash;
};

extern std::string current_user;
extern int current_uid;
extern int current_gid;
extern std::string current_home;

void login_loop();
bool login_once();
bool load_user_from_files(const std::string& username, LoginUser& user);
std::string read_password(const std::string& prompt);
bool verify_password(const std::string& password, const std::string& hash);
std::vector<std::string> split_string(const std::string& text, char sep);
std::string make_prompt();

void cmd_su(const CommandContext& ctx);
void cmd_logout(const CommandContext& ctx);
void cmd_id(const CommandContext& ctx);
void cmd_whoami(const CommandContext& ctx);

void cmd_mount(const CommandContext& ctx);
void cmd_umount(const CommandContext& ctx);

void sys_poweroff(const CommandContext& ctx);
void sys_reboot(const CommandContext& ctx);
