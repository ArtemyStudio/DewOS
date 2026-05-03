#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <functional>
#include <algorithm>
#include <termios.h>

#define RESET       "\033[0m"
#define CYN         "\033[36m"
#define GNR         "\033[32m"
#define YLW         "\033[33m"
#define MAG         "\033[35m"
#define RED         "\033[31m"
#define BLUE        "\033[38;5;39m"
#define LIGHT_BLUE  "\033[38;5;45m"
#define BOLD        "\033[1m"

struct CommandContext {
    std::string name;
    std::vector<std::string> args;
    std::vector<std::string> flags;
    std::string raw;

    bool has_flag(const std::string& flag) const {
        return std::find(flags.begin(), flags.end(), flag) != flags.end();
    }
};

struct Command {
    std::string name;
    std::string description;
    std::function<void(const CommandContext&)> handler;
};

struct LoginUser {
    std::string name;
    int uid;
    int gid;
    std::string home;
    std::string shell;
    std::string password_hash;
};

std::string current_user = "root";
int current_uid = 0;
int current_gid = 0;
std::string current_home = "/root";

bool rootfs_ready = false;

static termios original_termios;

static std::vector<std::string> history;
static size_t history_index = 0;

static std::vector<std::string> scrollback;
static std::string scroll_current_line;
static int scroll_offset = 0;
static const int SCREEN_ROWS = 25;


void setup_console();
void mount_basic_fs();

void print_error(const std::string& message);
void shell_loop();
void critical_error(const char* message, uint32_t error_code);
void emergency_print(const char* str, int x, int y, uint8_t color);
void hex_to_str(uint32_t n, char* out);

CommandContext parse_command(const std::string& input);
std::vector<Command> create_commands();
void exec_command(const CommandContext& ctx, const std::vector<Command>& commands);

void cmd_about(const CommandContext& ctx);
void cmd_help(const CommandContext& ctx, const std::vector<Command>& commands);
void cmd_clear(const CommandContext& ctx);
void cmd_echo(const CommandContext& ctx);
void cmd_whoami(const CommandContext& ctx);
void cmd_id(const CommandContext& ctx);
void cmd_logout(const CommandContext& ctx);
void cmd_install(const CommandContext& ctx);
void cmd_umount(const CommandContext& ctx);
void print_banner();

void sys_poweroff(const CommandContext& ctx);
void sys_reboot(const CommandContext& ctx);

void cmd_pwd(const CommandContext& ctx);
void cmd_ls(const CommandContext& ctx);
void cmd_cd(const CommandContext& ctx);
void cmd_mkdir(const CommandContext& ctx);
void cmd_touch(const CommandContext& ctx);
void cmd_rm(const CommandContext& ctx);
void cmd_cat(const CommandContext& ctx);

void cmd_kilo(const CommandContext& ctx);
void kilo_editor(const CommandContext& ctx, const char* filename);

void cmd_dewfetch(const CommandContext& ctx);
void cmd_ps(const CommandContext& ctx);

void enable_raw_mode();
void disable_raw_mode();
std::string read_line(const std::string& prompt);
void redraw_line(const std::string& prompt, const std::string& line, size_t cursor, bool cursor_visible);
void raw_write(const std::string& text);

void scroll_up();
void scroll_down();
void redraw_screen_scroll();
void shell_print(const std::string& text);

void login_loop();
bool login_once();
bool load_user_from_files(const std::string& username, LoginUser& user);
std::string read_password(const std::string& prompt);
bool verify_password(const std::string& password, const std::string& hash);
std::vector<std::string> split_string(const std::string& text, char sep);
std::string make_prompt();

bool detect_rootfs();
bool require_rootfs(const std::string& command_name);

void cmd_sh(const CommandContext& ctx);

void cmd_su(const CommandContext& ctx);

void start_network_if_installed();
void cmd_netlog(const CommandContext& ctx);

void cmd_unknown(const CommandContext& ctx);