#include <algorithm>
#include <cerrno>
#include <cstdint>
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
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

namespace ui {
    constexpr const char* RESET     = "\033[0m";
    constexpr const char* BOLD      = "\033[1m";
    constexpr const char* DIM       = "\033[2m";
    constexpr const char* INVERT    = "\033[7m";
    constexpr const char* FG_WHITE  = "\033[97m";
    constexpr const char* FG_GREY   = "\033[90m";
    constexpr const char* FG_GREY2  = "\033[37m";
    constexpr const char* FG_ACCENT = "\033[96m";

    constexpr const char* HIDE_CURSOR = "\033[?25l";
    constexpr const char* SHOW_CURSOR = "\033[?25h";
    constexpr const char* CLEAR       = "\033[2J\033[H";
}

struct Term {
    termios saved{};
    bool    raw_active = false;
    int     rows = 24;
    int     cols = 80;

    void query_size() {
        winsize ws{};

        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
            rows = ws.ws_row;
            cols = ws.ws_col;
        }
    }

    void enable_raw() {
        if (raw_active) return;
        if (tcgetattr(STDIN_FILENO, &saved) < 0) return;
        termios r = saved;
        r.c_lflag &= ~(ICANON | ECHO);
        r.c_cc[VMIN] = 1;
        r.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &r);
        raw_active = true;
    }

    void disable_raw() {
        if (!raw_active) return;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
        raw_active = false;
    }
};

static Term term;

static void out(const std::string& s) {
    const char* p = s.data();
    size_t left = s.size();
    while (left > 0) {
        ssize_t n = write(STDOUT_FILENO, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            return;
        }
        p += n;
        left -= static_cast<size_t>(n);
    }
}

static void move_to(int row, int col) {
    out("\033[" + std::to_string(row) + ";" + std::to_string(col) + "H");
}

static int read_key() {
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) != 1) return -1;

    if (c == '\033') {
        char seq[2] = {0, 0};
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return 27;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return 27;
        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': return 1001; 
                case 'B': return 1002; 
                case 'C': return 1003;
                case 'D': return 1004; 
            }
        }
        return 27;
    }

    return static_cast<unsigned char>(c);
}


// ──────────────────────────────────────────────────────────────────────
// Layout primitives
// ──────────────────────────────────────────────────────────────────────
static void clear_screen() {
    out(ui::CLEAR);
}

static void draw_frame(const std::string& title) {
    clear_screen();
    out(ui::HIDE_CURSOR);

    int w = term.cols;
    int h = term.rows;

    // top bar
    move_to(1, 1);
    out(ui::INVERT);
    out(ui::BOLD);
    std::string bar = " DewOS Installer  ";
    bar += "─ ";
    bar += title;
    bar += std::string(std::max(0, w - (int)bar.size()), ' ');
    out(bar);
    out(ui::RESET);

    // bottom bar with hints
    move_to(h, 1);
    out(ui::DIM);
    std::string hints = " ↑/↓ select   ENTER confirm   ESC back   Ctrl-C abort ";
    if ((int)hints.size() > w) hints = hints.substr(0, w);
    hints += std::string(std::max(0, w - (int)hints.size()), ' ');
    out(hints);
    out(ui::RESET);
}

static void say(int row, const std::string& text, const char* style = "") {
    move_to(row, 3);
    out("\033[K"); // clear line
    out(style);
    out(text);
    out(ui::RESET);
}


// ──────────────────────────────────────────────────────────────────────
// Helpers: shell exec
// ──────────────────────────────────────────────────────────────────────
struct ExecResult {
    int  status = -1;
    std::string out;
};

// Run command, capture stdout+stderr, no shell parsing — argv style.
static ExecResult run_capture(const std::vector<std::string>& argv) {
    ExecResult r;
    int pipefd[2];
    if (pipe(pipefd) < 0) return r;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]); close(pipefd[1]);
        return r;
    }

    if (pid == 0) {
        // child
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        std::vector<char*> args;
        args.reserve(argv.size() + 1);
        for (auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
        args.push_back(nullptr);

        execvp(args[0], args.data());
        _exit(127);
    }

    close(pipefd[1]);
    char buf[1024];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        r.out.append(buf, n);
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) r.status = WEXITSTATUS(status);
    return r;
}

// Run command, no capture, child inherits stdout/stderr.
static int run_visible(const std::vector<std::string>& argv) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        std::vector<char*> args;
        args.reserve(argv.size() + 1);
        for (auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
        args.push_back(nullptr);
        execvp(args[0], args.data());
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

static std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> v;
    std::stringstream ss(s);
    std::string line;
    while (std::getline(ss, line)) v.push_back(line);
    return v;
}


// ──────────────────────────────────────────────────────────────────────
// Disk discovery
// ──────────────────────────────────────────────────────────────────────
struct Disk {
    std::string name;     // sda, vda, nvme0n1
    std::string path;     // /dev/sda
    std::string size_str; // "32G"
    std::string model;    // "QEMU HARDDISK"
};

static std::vector<Disk> list_disks() {
    std::vector<Disk> disks;

    // lsblk is the cleanest. Format: NAME SIZE TYPE MODEL
    ExecResult r = run_capture({"lsblk", "-d", "-n", "-o", "NAME,SIZE,TYPE,MODEL"});
    if (r.status == 0) {
        for (auto& line : split_lines(r.out)) {
            if (line.empty()) continue;

            std::stringstream ss(line);
            std::string name, size, type;
            ss >> name >> size >> type;
            std::string model;
            std::getline(ss, model);
            // trim
            size_t a = model.find_first_not_of(" \t");
            if (a != std::string::npos) model = model.substr(a);

            if (type != "disk") continue;

            Disk d;
            d.name = name;
            d.path = "/dev/" + name;
            d.size_str = size;
            d.model = model.empty() ? "(unknown)" : model;
            disks.push_back(d);
        }
        return disks;
    }

    // Fallback: scan /sys/block
    DIR* dir = opendir("/sys/block");
    if (dir) {
        struct dirent* e;
        while ((e = readdir(dir)) != nullptr) {
            std::string n = e->d_name;
            if (n == "." || n == "..") continue;
            // skip loop, ram, sr (cdrom)
            if (n.rfind("loop", 0) == 0) continue;
            if (n.rfind("ram", 0) == 0) continue;
            if (n.rfind("sr", 0) == 0) continue;

            Disk d;
            d.name = n;
            d.path = "/dev/" + n;
            d.size_str = "?";
            d.model = "(unknown)";
            disks.push_back(d);
        }
        closedir(dir);
    }
    return disks;
}


// ──────────────────────────────────────────────────────────────────────
// Menu (vertical select)
// ──────────────────────────────────────────────────────────────────────
// Returns selected index, or -1 on ESC.
static int menu(const std::string& title, const std::string& subtitle,
                const std::vector<std::string>& items) {
    int idx = 0;
    while (true) {
        draw_frame(title);
        say(3, subtitle, ui::FG_GREY2);

        for (size_t i = 0; i < items.size(); ++i) {
            move_to(5 + (int)i, 5);
            if ((int)i == idx) {
                out(ui::FG_ACCENT);
                out(ui::BOLD);
                out("▸ ");
                out(items[i]);
                out(ui::RESET);
            } else {
                out(ui::FG_GREY2);
                out("  ");
                out(items[i]);
                out(ui::RESET);
            }
        }

        int k = read_key();
        if (k == 1001) idx = (idx - 1 + items.size()) % items.size();
        else if (k == 1002) idx = (idx + 1) % items.size();
        else if (k == '\n' || k == '\r') return idx;
        else if (k == 27) return -1;
    }
}


// ──────────────────────────────────────────────────────────────────────
// Prompt input (single line, with echo or hidden)
// ──────────────────────────────────────────────────────────────────────
// Returns "" on ESC or empty input; sets ok=false on ESC.
static std::string prompt(const std::string& title, const std::string& label,
                          bool hidden, bool& ok) {
    ok = true;
    draw_frame(title);
    say(3, label, ui::FG_GREY2);
    move_to(5, 5);
    out(ui::FG_ACCENT);
    out("❯ ");
    out(ui::RESET);
    out(ui::SHOW_CURSOR);

    std::string buf;
    while (true) {
        int k = read_key();
        if (k == '\n' || k == '\r') {
            out(ui::HIDE_CURSOR);
            return buf;
        }
        if (k == 27) {
            ok = false;
            out(ui::HIDE_CURSOR);
            return "";
        }
        if (k == 127 || k == '\b') {
            if (!buf.empty()) {
                buf.pop_back();
                out("\b \b");
            }
            continue;
        }
        if (k >= 32 && k <= 126) {
            buf.push_back(static_cast<char>(k));
            if (hidden) out("*");
            else { char ch = static_cast<char>(k); out(std::string(1, ch)); }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// Progress screen + log
// ──────────────────────────────────────────────────────────────────────
struct Progress {
    std::vector<std::string> log;
    int row_top = 5;

    void add(const std::string& msg, bool ok_mark = true, bool fail = false) {
        std::string prefix = fail ? "  ✗ " : (ok_mark ? "  ✓ " : "  · ");
        log.push_back(prefix + msg);
        redraw();
    }

    void redraw() {
        draw_frame("Installing");
        say(3, "DewOS is being installed. Sit tight.", ui::FG_GREY2);

        int max_rows = term.rows - row_top - 2;
        int start = std::max(0, (int)log.size() - max_rows);
        for (size_t i = start; i < log.size(); ++i) {
            move_to(row_top + (int)(i - start), 3);
            const auto& line = log[i];
            if (line.size() > 4 && line[2] == '\xe2') {
                // "✓" or "✗" — bytes are multibyte
                // colorize just the mark
                std::string marker = line.substr(0, 5);
                std::string rest   = line.substr(5);
                if (line.find("\xe2\x9c\x97") != std::string::npos) out(ui::FG_GREY);
                else if (line.find("\xe2\x9c\x93") != std::string::npos) out(ui::FG_ACCENT);
                out(marker);
                out(ui::RESET);
                out(ui::FG_GREY2);
                out(rest);
                out(ui::RESET);
            } else {
                out(ui::FG_GREY2);
                out(line);
                out(ui::RESET);
            }
        }
    }
};


// ──────────────────────────────────────────────────────────────────────
// Installer steps
// ──────────────────────────────────────────────────────────────────────
struct Config {
    std::string disk;       // /dev/sda
    std::string hostname;
    std::string username;
    std::string password;
};

// Wipe + GPT + 1 ESP (512M, vfat) + 1 root (rest, ext4)
static bool partition_disk(Progress& p, const std::string& disk) {
    p.add("Wiping signatures on " + disk + " ...", false);
    run_visible({"wipefs", "-a", disk});

    // Build sfdisk script. GPT, ESP 512M, rest root.
    p.add("Creating GPT layout (ESP 512M + root rest) ...", false);

    // We use fdisk piped via shell-less write. Simpler: write a temp script and pipe.
    std::string script =
        "label: gpt\n"
        ",512M,U,*\n"
        ",,L\n";

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        p.add("pipe() failed", true, true);
        return false;
    }
    pid_t pid = fork();
    if (pid < 0) {
        p.add("fork() failed", true, true);
        close(pipefd[0]); close(pipefd[1]);
        return false;
    }
    if (pid == 0) {
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        execlp("sfdisk", "sfdisk", "--wipe", "always", disk.c_str(), nullptr);
        _exit(127);
    }
    close(pipefd[0]);
    (void)!write(pipefd[1], script.data(), script.size());
    close(pipefd[1]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        p.add("sfdisk failed (status " + std::to_string(WEXITSTATUS(status)) + ")", true, true);
        return false;
    }

    run_visible({"partprobe", disk});
    run_visible({"udevadm", "settle"});
    return true;
}

static std::pair<std::string, std::string> part_names(const std::string& disk) {
    // /dev/sda → /dev/sda1, /dev/sda2
    // /dev/nvme0n1 → /dev/nvme0n1p1, /dev/nvme0n1p2
    char last = disk.empty() ? 0 : disk.back();
    std::string sep = (last >= '0' && last <= '9') ? "p" : "";
    return {disk + sep + "1", disk + sep + "2"};
}

static bool format_partitions(Progress& p, const std::string& disk) {
    auto [esp, root] = part_names(disk);

    p.add("Formatting " + esp + " as FAT32 (ESP) ...", false);
    if (run_visible({"mkfs.vfat", "-F", "32", esp}) != 0) {
        p.add("mkfs.vfat failed", true, true);
        return false;
    }

    p.add("Formatting " + root + " as ext4 ...", false);
    if (run_visible({"mkfs.ext4", "-F", root}) != 0) {
        p.add("mkfs.ext4 failed", true, true);
        return false;
    }
    return true;
}

static bool mount_target(Progress& p, const std::string& disk) {
    auto [esp, root] = part_names(disk);

    mkdir("/mnt/target", 0755);
    p.add("Mounting " + root + " at /mnt/target ...", false);
    if (run_visible({"mount", root, "/mnt/target"}) != 0) {
        p.add("mount root failed", true, true);
        return false;
    }

    mkdir("/mnt/target/boot", 0755);
    mkdir("/mnt/target/boot/efi", 0755);
    p.add("Mounting " + esp + " at /mnt/target/boot/efi ...", false);
    if (run_visible({"mount", esp, "/mnt/target/boot/efi"}) != 0) {
        p.add("mount ESP failed (continuing in BIOS mode)", true, false);
        // not fatal — GRUB BIOS can still install
    }
    return true;
}

static bool copy_rootfs(Progress& p) {
    if (access("/rootfs-disk", F_OK) != 0) {
        // No template provided. Create a minimal one from initramfs.
        p.add("No /rootfs-disk template — building minimal one", false);

        const char* dirs[] = {
            "/mnt/target/bin", "/mnt/target/sbin", "/mnt/target/etc",
            "/mnt/target/dev", "/mnt/target/proc", "/mnt/target/sys",
            "/mnt/target/tmp", "/mnt/target/root", "/mnt/target/home",
            "/mnt/target/var", "/mnt/target/run", "/mnt/target/usr",
            "/mnt/target/usr/bin", "/mnt/target/usr/sbin",
            "/mnt/target/lib", "/mnt/target/lib64", "/mnt/target/mnt",
            "/mnt/target/boot",
            "/mnt/target/var/drop", "/mnt/target/var/drop/installed",
            nullptr
        };
        for (int i = 0; dirs[i]; ++i) mkdir(dirs[i], 0755);

        // Copy from initramfs root, minus pseudo-fs and mount points.
        // We use sh+cpio for simplicity.
        const char* script =
            "cd / && "
            "find . -xdev "
            "-not -path './proc/*' -not -path './sys/*' -not -path './dev/*' "
            "-not -path './tmp/*' -not -path './mnt/*' -not -path './run/*' "
            "-not -path './rootfs-disk*' "
            "| cpio --quiet -pdm /mnt/target";
        if (run_visible({"sh", "-c", script}) != 0) {
            p.add("rootfs copy failed", true, true);
            return false;
        }
    } else {
        p.add("Copying /rootfs-disk → /mnt/target ...", false);
        if (run_visible({"sh", "-c",
            "cd /rootfs-disk && find . -xdev | cpio --quiet -pdm /mnt/target"}) != 0) {
            p.add("rootfs copy failed", true, true);
            return false;
        }
    }
    return true;
}

static bool write_system_files(Progress& p, const Config& cfg) {
    p.add("Writing /etc/hostname ...", false);
    {
        std::ofstream f("/mnt/target/etc/hostname");
        f << cfg.hostname << "\n";
    }

    p.add("Writing /etc/passwd, /etc/shadow ...", false);
    {
        std::string hash = cfg.password;

        std::ofstream passwd("/mnt/target/etc/passwd");
        passwd << "root:x:0:0:root:/root:/init\n";
        passwd << cfg.username << ":x:1000:1000:" << cfg.username
               << ":/home/" << cfg.username << ":/init\n";

        std::ofstream group("/mnt/target/etc/group");
        group << "root:x:0:\n";
        group << cfg.username << ":x:1000:\n";

        std::ofstream shadow("/mnt/target/etc/shadow");
        shadow << "root:" << hash << ":0:0:99999:7:::\n";
        shadow << cfg.username << ":" << hash << ":0:0:99999:7:::\n";

        chmod("/mnt/target/etc/shadow", 0600);
    }

    // home dir
    std::string home = "/mnt/target/home/" + cfg.username;
    mkdir(home.c_str(), 0755);

    // mark installed
    p.add("Marking system as installed (/etc/dewos-installed) ...", false);
    {
        std::ofstream f("/mnt/target/etc/dewos-installed");
        f << "version=0.1\n";
        f << "installed_by=dew-install\n";
    }

    return true;
}

static bool install_grub(Progress& p, const std::string& disk) {
    p.add("Installing GRUB to " + disk + " ...", false);

    // Try EFI first if /sys/firmware/efi exists
    bool efi = (access("/sys/firmware/efi", F_OK) == 0);

    mkdir("/mnt/target/boot/grub", 0755);

    if (efi) {
        // bind-mount /dev /proc /sys for grub-install
        run_visible({"mount", "--bind", "/dev", "/mnt/target/dev"});
        run_visible({"mount", "--bind", "/proc", "/mnt/target/proc"});
        run_visible({"mount", "--bind", "/sys", "/mnt/target/sys"});

        int rc = run_visible({"grub-install", "--target=x86_64-efi",
                              "--efi-directory=/mnt/target/boot/efi",
                              "--boot-directory=/mnt/target/boot",
                              "--removable", "--no-nvram"});
        if (rc != 0) {
            p.add("grub-install (EFI) failed, falling back to BIOS", true, false);
            efi = false;
        }
    }

    if (!efi) {
        int rc = run_visible({"grub-install", "--target=i386-pc",
                              "--boot-directory=/mnt/target/boot", disk});
        if (rc != 0) {
            p.add("grub-install (BIOS) failed", true, true);
            return false;
        }
    }

    // Write grub.cfg pointing at our kernel + initramfs.
    p.add("Writing /boot/grub/grub.cfg ...", false);
    {
        std::ofstream f("/mnt/target/boot/grub/grub.cfg");
        f << "set default=0\n";
        f << "set timeout=3\n";
        f << "menuentry \"DewOS\" {\n";
        f << "    linux /boot/vmlinuz-dewos root=" << disk << "2 rdinit=/init rw quiet\n";
        f << "    initrd /boot/initramfs.cpio.gz\n";
        f << "}\n";
    }

    // Copy kernel + initramfs from live
    if (access("/boot/vmlinuz-dewos", F_OK) == 0) {
        run_visible({"cp", "/boot/vmlinuz-dewos", "/mnt/target/boot/vmlinuz-dewos"});
    }
    if (access("/boot/initramfs.cpio.gz", F_OK) == 0) {
        run_visible({"cp", "/boot/initramfs.cpio.gz", "/mnt/target/boot/initramfs.cpio.gz"});
    }

    return true;
}

static void unmount_all() {
    run_visible({"sync"});
    run_visible({"umount", "/mnt/target/sys"});
    run_visible({"umount", "/mnt/target/proc"});
    run_visible({"umount", "/mnt/target/dev"});
    run_visible({"umount", "/mnt/target/boot/efi"});
    run_visible({"umount", "/mnt/target"});
}


// ──────────────────────────────────────────────────────────────────────
// Screens
// ──────────────────────────────────────────────────────────────────────
static bool screen_welcome() {
    while (true) {
        int v = menu(
            "Welcome",
            "Install DewOS to a real disk. This will wipe everything on the selected drive.",
            {"Install DewOS", "Open a shell", "Reboot", "Power off"}
        );
        if (v == 0) return true;
        if (v == 1) {
            term.disable_raw();
            out(ui::SHOW_CURSOR);
            run_visible({"sh"});
            term.enable_raw();
            out(ui::HIDE_CURSOR);
            continue;
        }
        if (v == 2) { run_visible({"reboot"}); return false; }
        if (v == 3) { run_visible({"poweroff"}); return false; }
        if (v == -1) continue; // ESC on welcome stays here
    }
}

static bool screen_pick_disk(Config& cfg) {
    auto disks = list_disks();
    if (disks.empty()) {
        menu("No disks", "No disks were detected. Press ENTER to go back.", {"Back"});
        return false;
    }

    std::vector<std::string> items;
    for (auto& d : disks) {
        std::string line = d.path + "   " + d.size_str + "   " + d.model;
        items.push_back(line);
    }
    items.push_back("Cancel");

    int v = menu("Select target disk",
                 "All data on the chosen disk will be erased.",
                 items);
    if (v < 0 || v == (int)disks.size()) return false;
    cfg.disk = disks[v].path;
    return true;
}

static bool screen_user(Config& cfg) {
    bool ok;
    while (true) {
        cfg.hostname = prompt("Hostname", "Pick a hostname (e.g. dewbox)", false, ok);
        if (!ok) return false;
        if (cfg.hostname.empty()) continue;
        break;
    }
    while (true) {
        cfg.username = prompt("User", "Username for your account (lowercase, no spaces)", false, ok);
        if (!ok) return false;
        if (cfg.username.empty()) continue;
        // basic validation
        bool good = true;
        for (char c : cfg.username) {
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
                good = false; break;
            }
        }
        if (!good) continue;
        break;
    }
    while (true) {
        cfg.password = prompt("Password", "Password (hidden as you type)", true, ok);
        if (!ok) return false;
        if (cfg.password.empty()) continue;
        std::string confirm_pw = prompt("Password", "Confirm password", true, ok);
        if (!ok) return false;
        if (confirm_pw != cfg.password) {
            menu("Mismatch", "Passwords don't match. Try again.", {"OK"});
            continue;
        }
        break;
    }
    return true;
}

static bool screen_confirm(const Config& cfg) {
    draw_frame("Summary");
    say(3, "Review your choices before installation begins.", ui::FG_GREY2);
    say(5,  std::string("disk     ") + cfg.disk,     ui::FG_WHITE);
    say(6,  std::string("hostname ") + cfg.hostname, ui::FG_WHITE);
    say(7,  std::string("user     ") + cfg.username, ui::FG_WHITE);
    say(9,  "Everything on the disk will be PERMANENTLY erased.", ui::FG_GREY);

    while (true) {
        move_to(11, 5);
        out(ui::FG_ACCENT);
        out(ui::BOLD);
        out("Type INSTALL to proceed (or ESC to go back): ");
        out(ui::RESET);
        out(ui::SHOW_CURSOR);

        std::string buf;
        while (true) {
            int k = read_key();
            if (k == 27) { out(ui::HIDE_CURSOR); return false; }
            if (k == '\n' || k == '\r') break;
            if (k == 127 || k == '\b') {
                if (!buf.empty()) { buf.pop_back(); out("\b \b"); }
                continue;
            }
            if (k >= 32 && k <= 126) {
                buf.push_back(static_cast<char>(k));
                char ch = static_cast<char>(k);
                out(std::string(1, ch));
            }
        }
        out(ui::HIDE_CURSOR);
        if (buf == "INSTALL") return true;
    }
}

static bool screen_install(const Config& cfg) {
    Progress p;
    p.add("Starting install on " + cfg.disk, false);

    if (!partition_disk(p, cfg.disk))   return false;
    if (!format_partitions(p, cfg.disk))return false;
    if (!mount_target(p, cfg.disk))     return false;
    if (!copy_rootfs(p))                return false;
    if (!write_system_files(p, cfg))    return false;
    if (!install_grub(p, cfg.disk))     return false;

    p.add("Syncing and unmounting ...", false);
    unmount_all();
    p.add("Done.", true);
    return true;
}

static void screen_done() {
    int v = menu(
        "Done",
        "DewOS is installed. Remove the install media before rebooting.",
        {"Reboot now", "Power off", "Drop to shell"}
    );
    if (v == 0) run_visible({"reboot"});
    else if (v == 1) run_visible({"poweroff"});
    else {
        term.disable_raw();
        out(ui::SHOW_CURSOR);
        run_visible({"sh"});
    }
}


// ──────────────────────────────────────────────────────────────────────
// Main
// ──────────────────────────────────────────────────────────────────────
int main() {
    term.query_size();
    term.enable_raw();
    out(ui::HIDE_CURSOR);

    if (!screen_welcome()) {
        out(ui::SHOW_CURSOR);
        term.disable_raw();
        return 0;
    }

    Config cfg;
    while (true) {
        if (!screen_pick_disk(cfg)) {
            if (!screen_welcome()) break;
            continue;
        }
        if (!screen_user(cfg)) continue;
        if (!screen_confirm(cfg)) continue;
        if (screen_install(cfg)) {
            screen_done();
            break;
        } else {
            menu("Install failed",
                 "Something went wrong. Drop to shell to inspect, then reboot.",
                 {"OK"});
            term.disable_raw();
            out(ui::SHOW_CURSOR);
            run_visible({"sh"});
            break;
        }
    }

    out(ui::SHOW_CURSOR);
    out(ui::RESET);
    term.disable_raw();
    return 0;
}
