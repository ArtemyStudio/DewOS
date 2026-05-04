#pragma once

#include <string>
#include <cstdint>

extern bool rootfs_ready;

void setup_console();
void mount_basic_fs();
void show_boot_splash();

void print_error(const std::string& message);
void critical_error(const char* message, uint32_t error_code);
void emergency_print(const char* str, int x, int y, uint8_t color);
void hex_to_str(uint32_t n, char* out);
