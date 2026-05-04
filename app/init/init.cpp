#include "../include/init.hpp"
#include "../include/system.hpp"
#include "../include/shell.hpp"

#include <cerrno>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/sysmacros.h>

bool rootfs_ready = false;


int main() {
    setup_console();
    raw_write("\033[?25l");
    raw_write("\033[2J\033[H");
    raw_write("[DewOS] console ready\n");

    mount_basic_fs();
    raw_write("[DewOS] basic filesystems mounted\n");

    rootfs_ready = detect_rootfs();

    show_boot_splash();

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


void show_boot_splash() {
    int fb = open("/dev/fb0", O_RDWR);

    if (fb >= 0) {
        fb_fix_screeninfo finfo{};
        fb_var_screeninfo vinfo{};

        if (ioctl(fb, FBIOGET_FSCREENINFO, &finfo) == 0 && ioctl(fb, FBIOGET_VSCREENINFO, &vinfo) == 0) {
            int bytes_per_pixel = vinfo.bits_per_pixel / 8;

            if ((vinfo.bits_per_pixel == 16 || vinfo.bits_per_pixel == 24 || vinfo.bits_per_pixel == 32) &&
                bytes_per_pixel > 0 && finfo.line_length > 0 && vinfo.xres > 0 && vinfo.yres > 0) {
                size_t screen_size = finfo.smem_len;
                if (screen_size == 0) {
                    screen_size = finfo.line_length * vinfo.yres;
                }

                uint8_t* screen = (uint8_t*)mmap(nullptr, screen_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);

                if (screen != MAP_FAILED) {
                    auto pack_channel = [](uint8_t value, fb_bitfield field) -> uint32_t {
                        if (field.length == 0) {
                            return 0;
                        }

                        uint32_t max_value = (1u << field.length) - 1u;
                        return ((uint32_t)value * max_value / 255u) << field.offset;
                    };

                    auto pixel_value = [&](uint8_t r, uint8_t g, uint8_t b) -> uint32_t {
                        uint32_t pixel = 0;
                        pixel |= pack_channel(r, vinfo.red);
                        pixel |= pack_channel(g, vinfo.green);
                        pixel |= pack_channel(b, vinfo.blue);
                        return pixel;
                    };

                    auto put_pixel = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
                        if (x < 0 || y < 0 || x >= (int)vinfo.xres || y >= (int)vinfo.yres) {
                            return;
                        }

                        size_t pos = y * finfo.line_length + x * bytes_per_pixel;
                        if (pos + bytes_per_pixel > screen_size) {
                            return;
                        }

                        uint32_t color = pixel_value(r, g, b);

                        if (vinfo.bits_per_pixel == 32) {
                            *(uint32_t*)(screen + pos) = color;
                        } else if (vinfo.bits_per_pixel == 24) {
                            screen[pos + (vinfo.red.offset / 8)] = r;
                            screen[pos + (vinfo.green.offset / 8)] = g;
                            screen[pos + (vinfo.blue.offset / 8)] = b;
                        } else if (vinfo.bits_per_pixel == 16) {
                            *(uint16_t*)(screen + pos) = (uint16_t)color;
                        }
                    };

                    auto rect = [&](int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
                        for (int yy = y; yy < y + h; ++yy) {
                            for (int xx = x; xx < x + w; ++xx) {
                                put_pixel(xx, yy, r, g, b);
                            }
                        }
                    };

                    struct PpmImage {
                        int width = 0;
                        int height = 0;
                        std::vector<uint8_t> pixels;
                    };

                    auto read_token = [](std::ifstream& file, std::string& token) -> bool {
                        char ch = 0;
                        token.clear();

                        while (file.get(ch)) {
                            if (ch == '#') {
                                file.ignore(4096, '\n');
                            } else if (ch > ' ') {
                                token += ch;
                                break;
                            }
                        }

                        while (file.get(ch)) {
                            if (ch == '#') {
                                file.ignore(4096, '\n');
                                break;
                            }
                            if (ch <= ' ') {
                                break;
                            }
                            token += ch;
                        }

                        return !token.empty();
                    };

                    auto to_int = [](const std::string& text) -> int {
                        int value = 0;

                        if (text.empty()) {
                            return -1;
                        }

                        for (char ch : text) {
                            if (ch < '0' || ch > '9') {
                                return -1;
                            }

                            value = value * 10 + (ch - '0');
                        }

                        return value;
                    };

                    auto load_ppm = [&](const std::string& path) -> PpmImage {
                        PpmImage image;
                        std::ifstream file(path, std::ios::binary);

                        if (!file.is_open()) {
                            return image;
                        }

                        std::string token;
                        if (!read_token(file, token) || token != "P6") {
                            return image;
                        }

                        if (!read_token(file, token)) {
                            return image;
                        }
                        image.width = to_int(token);

                        if (!read_token(file, token)) {
                            return image;
                        }
                        image.height = to_int(token);

                        if (!read_token(file, token) || to_int(token) != 255 ||
                            image.width <= 0 || image.height <= 0) {
                            image = {};
                            return image;
                        }

                        image.pixels.resize((size_t)image.width * image.height * 3);
                        file.read((char*)image.pixels.data(), image.pixels.size());

                        if ((size_t)file.gcount() != image.pixels.size()) {
                            image = {};
                        }

                        return image;
                    };

                    auto draw_image = [&](const PpmImage& image, int x0, int y0, int w, int h) {
                        if (image.pixels.empty() || w <= 0 || h <= 0) {
                            return;
                        }

                        for (int y = 0; y < h; ++y) {
                            int src_y = y * image.height / h;

                            for (int x = 0; x < w; ++x) {
                                int src_x = x * image.width / w;
                                size_t pos = ((size_t)src_y * image.width + src_x) * 3;
                                put_pixel(x0 + x, y0 + y,
                                          image.pixels[pos],
                                          image.pixels[pos + 1],
                                          image.pixels[pos + 2]);
                            }
                        }
                    };

                    rect(0, 0, vinfo.xres, vinfo.yres, 0, 0, 0);

                    PpmImage logo = load_ppm("/etc/dewos-logo.ppm");

                    if (!logo.pixels.empty()) {
                        int logo_h = vinfo.yres / 3;
                        int logo_w = logo.width * logo_h / logo.height;

                        if (logo_w > (int)vinfo.xres / 2) {
                            logo_w = vinfo.xres / 2;
                            logo_h = logo.height * logo_w / logo.width;
                        }

                        int logo_x = ((int)vinfo.xres - logo_w) / 2;
                        int logo_y = ((int)vinfo.yres / 2) - logo_h / 2 - 70;
                        draw_image(logo, logo_x, logo_y, logo_w, logo_h);
                    }

                    int loader_size = vinfo.yres / 12;
                    if (loader_size < 48) {
                        loader_size = 48;
                    }
                    if (loader_size > 96) {
                        loader_size = 96;
                    }

                    int loader_x = ((int)vinfo.xres - loader_size) / 2;
                    int loader_y = ((int)vinfo.yres / 2) + vinfo.yres / 5;
                    int loader_cx = loader_x + loader_size / 2;
                    int loader_cy = loader_y + loader_size / 2;
                    int loader_radius = loader_size / 2 - 6;
                    int loader_thick = loader_size / 16;

                    if (loader_thick < 4) {
                        loader_thick = 4;
                    }

                    const int circle_points[32][2] = {
                        {0, -100}, {20, -98}, {38, -92}, {56, -83},
                        {71, -71}, {83, -56}, {92, -38}, {98, -20},
                        {100, 0}, {98, 20}, {92, 38}, {83, 56},
                        {71, 71}, {56, 83}, {38, 92}, {20, 98},
                        {0, 100}, {-20, 98}, {-38, 92}, {-56, 83},
                        {-71, 71}, {-83, 56}, {-92, 38}, {-98, 20},
                        {-100, 0}, {-98, -20}, {-92, -38}, {-83, -56},
                        {-71, -71}, {-56, -83}, {-38, -92}, {-20, -98}
                    };

                    auto draw_round_pixel = [&](int x, int y, int radius, uint8_t r, uint8_t g, uint8_t b) {
                        for (int yy = -radius; yy <= radius; ++yy) {
                            for (int xx = -radius; xx <= radius; ++xx) {
                                if (xx * xx + yy * yy <= radius * radius) {
                                    put_pixel(x + xx, y + yy, r, g, b);
                                }
                            }
                        }
                    };

                    auto draw_loader_segment = [&](int x0, int y0, int x1, int y1,
                                                   uint8_t r, uint8_t g, uint8_t b) {
                        int dx = x1 > x0 ? x1 - x0 : x0 - x1;
                        int dy = y1 > y0 ? y1 - y0 : y0 - y1;
                        int sx = x0 < x1 ? 1 : -1;
                        int sy = y0 < y1 ? 1 : -1;
                        int err = dx - dy;

                        while (true) {
                            draw_round_pixel(x0, y0, loader_thick, r, g, b);

                            if (x0 == x1 && y0 == y1) {
                                break;
                            }

                            int e2 = err * 2;

                            if (e2 > -dy) {
                                err -= dy;
                                x0 += sx;
                            }

                            if (e2 < dx) {
                                err += dx;
                                y0 += sy;
                            }
                        }
                    };

                    for (int frame = 0; frame < 80; ++frame) {
                        rect(loader_x - 10, loader_y - 10, loader_size + 20, loader_size + 20, 0, 0, 0);

                        int start = frame % 32;

                        for (int part = 0; part < 11; ++part) {
                            int index = (start + part) % 32;
                            int next = (index + 1) % 32;
                            int power = 70 + part * 17;

                            if (power > 255) {
                                power = 255;
                            }

                            int x0 = loader_cx + circle_points[index][0] * loader_radius / 100;
                            int y0 = loader_cy + circle_points[index][1] * loader_radius / 100;
                            int x1 = loader_cx + circle_points[next][0] * loader_radius / 100;
                            int y1 = loader_cy + circle_points[next][1] * loader_radius / 100;

                            draw_loader_segment(x0, y0, x1, y1,
                                                10 + power / 8,
                                                85 + power / 2,
                                                140 + power / 3);
                        }

                        usleep(50000);
                    }

                    munmap(screen, screen_size);
                    close(fb);
                    raw_write("\033[2J\033[H");
                    return;
                }
            }
        }

        close(fb);
    }

    raw_write("\033[?25l");
    raw_write("\033[2J\033[H");
    raw_write("\033[10;30HDEWOS\n");

    const char* spinner[] = {"|", "/", "-", "\\"};
    for (int i = 0; i < 32; ++i) {
        raw_write("\033[20;40H");
        raw_write(spinner[i % 4]);
        usleep(45000);
    }

    raw_write("\033[2J\033[H");
}


//#######################################################################################//
//######                             CONSOLE SETUP                                 ######//
//#######################################################################################//

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

    if (mknod("/dev/fb0", S_IFCHR | 0600, makedev(29, 0)) < 0 && errno != EEXIST) {
        print_error("dev: failed to create /dev/fb0");
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


//#######################################################################################//
//######                             CRITICAL ERRORS                               ######//
//#######################################################################################//

void print_error(const std::string& message) {
    std::cerr << "ERROR: " << message << std::endl;
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
