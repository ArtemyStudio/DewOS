#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>
#include <termios.h>

struct CommandContext {
    std::string name;
    std::vector<std::string> args;
    std::vector<std::string> flags;
    std::string raw;

    bool has_flag(const std::string& flag) const {
        for (const std::string& current : flags) {
            if (current == flag) {
                return true;
            }
        }

        return false;
    }
};

struct Command {
    std::string name;
    std::string description;
    std::function<void(const CommandContext&)> handler;
};

extern termios original_termios;

extern std::vector<std::string> history;
extern size_t history_index;

void shell_loop();
CommandContext parse_command(const std::string& input);
std::vector<Command> create_commands();
void exec_command(const CommandContext& ctx, const std::vector<Command>& commands);

void cmd_about(const CommandContext& ctx);
void cmd_help(const CommandContext& ctx, const std::vector<Command>& commands);
void cmd_clear(const CommandContext& ctx);
void cmd_echo(const CommandContext& ctx);
void cmd_install(const CommandContext& ctx);
void print_banner();

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

void shell_print(const std::string& text);

bool detect_rootfs();
bool require_rootfs(const std::string& command_name);

void cmd_sh(const CommandContext& ctx);
void cmd_drop(const CommandContext& ctx);

void start_network_if_installed();
void cmd_netlog(const CommandContext& ctx);

void unknown_command(const CommandContext& ctx);
