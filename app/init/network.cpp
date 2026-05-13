#include "../include/network.hpp"
#include "../include/system.hpp"
#include "../include/colors.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

struct RunResult {
    int code = 127;
    std::string output;
};

struct NetInterface {
    std::string name;
    bool wifi = false;
    std::string state;
    std::string driver;
};

static bool file_exists(const std::string& path) {
    return access(path.c_str(), F_OK) == 0;
}

static std::string trim(const std::string& text) {
    size_t start = 0;
    size_t end = text.size();

    while (start < end && text[start] <= ' ') {
        start++;
    }

    while (end > start && text[end - 1] <= ' ') {
        end--;
    }

    return text.substr(start, end - start);
}

static std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::stringstream ss(text);
    std::string line;

    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    return lines;
}

static std::string read_first_line(const std::string& path) {
    std::ifstream file(path);
    std::string line;

    if (file.is_open()) {
        std::getline(file, line);
    }

    return trim(line);
}

static std::string find_command(const std::string& name) {
    const char* dirs[] = {"/bin", "/sbin", "/usr/bin", "/usr/sbin"};

    for (const char* dir : dirs) {
        std::string path = std::string(dir) + "/" + name;

        if (access(path.c_str(), X_OK) == 0) {
            return path;
        }
    }

    return "";
}

static bool command_exists(const std::string& name) {
    return !find_command(name).empty();
}

static RunResult run_command(const std::vector<std::string>& args) {
    RunResult result;

    if (args.empty()) {
        return result;
    }

    std::string path = args[0].find('/') == std::string::npos ? find_command(args[0]) : args[0];

    if (path.empty()) {
        result.output = args[0] + ": command not found\n";
        return result;
    }

    int pipe_fd[2];

    if (pipe(pipe_fd) < 0) {
        result.output = "pipe failed: " + std::string(strerror(errno)) + "\n";
        return result;
    }

    pid_t pid = fork();

    if (pid < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        result.output = "fork failed: " + std::string(strerror(errno)) + "\n";
        return result;
    }

    if (pid == 0) {
        close(pipe_fd[0]);
        dup2(pipe_fd[1], STDOUT_FILENO);
        dup2(pipe_fd[1], STDERR_FILENO);

        if (pipe_fd[1] > STDERR_FILENO) {
            close(pipe_fd[1]);
        }

        std::vector<char*> exec_args;
        exec_args.push_back((char*)path.c_str());

        for (size_t i = 1; i < args.size(); ++i) {
            exec_args.push_back((char*)args[i].c_str());
        }

        exec_args.push_back(nullptr);
        execv(path.c_str(), exec_args.data());
        _exit(127);
    }

    close(pipe_fd[1]);

    char buffer[1024];
    ssize_t n;

    while ((n = read(pipe_fd[0], buffer, sizeof(buffer))) > 0) {
        result.output.append(buffer, n);
    }

    close(pipe_fd[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        result.code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.code = 128 + WTERMSIG(status);
    }

    return result;
}

static void print_command(const std::vector<std::string>& args) {
    if (args.empty()) {
        return;
    }

    shell_print(std::string(CYN) + "$ " + RESET);

    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            shell_print(" ");
        }

        shell_print(args[i]);
    }

    shell_print("\n");

    RunResult result = run_command(args);

    if (!result.output.empty()) {
        shell_print(result.output);
    }

    if (result.code != 0) {
        shell_print(std::string(RED) + "exit code: " + std::to_string(result.code) + RESET + "\n");
    }
}

static std::string driver_for_interface(const std::string& iface) {
    std::string path = "/sys/class/net/" + iface + "/device/driver";
    char link_target[512];
    ssize_t len = readlink(path.c_str(), link_target, sizeof(link_target) - 1);

    if (len < 0) {
        return "unknown";
    }

    link_target[len] = '\0';
    std::string driver = link_target;
    size_t slash = driver.find_last_of('/');

    if (slash != std::string::npos) {
        driver = driver.substr(slash + 1);
    }

    return driver.empty() ? "unknown" : driver;
}

static bool interface_is_wifi(const std::string& iface) {
    std::string base = "/sys/class/net/" + iface;
    return file_exists(base + "/wireless") || file_exists(base + "/phy80211");
}

static std::vector<NetInterface> list_interfaces() {
    std::vector<NetInterface> interfaces;
    DIR* dir = opendir("/sys/class/net");

    if (!dir) {
        return interfaces;
    }

    dirent* entry;

    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;

        if (name == "." || name == "..") {
            continue;
        }

        NetInterface iface;
        iface.name = name;
        iface.wifi = interface_is_wifi(name);
        iface.state = read_first_line("/sys/class/net/" + name + "/operstate");
        iface.driver = driver_for_interface(name);
        interfaces.push_back(iface);
    }

    closedir(dir);

    std::sort(interfaces.begin(), interfaces.end(), [](const NetInterface& a, const NetInterface& b) {
        return a.name < b.name;
    });

    return interfaces;
}

static std::string first_wifi_interface() {
    for (const NetInterface& iface : list_interfaces()) {
        if (iface.wifi) {
            return iface.name;
        }
    }

    if (command_exists("iw")) {
        RunResult iw = run_command({"iw", "dev"});

        for (const std::string& line : split_lines(iw.output)) {
            std::string clean = trim(line);

            if (clean.find("Interface ") == 0) {
                return clean.substr(10);
            }
        }
    }

    return "";
}

static std::string first_network_interface() {
    for (const NetInterface& iface : list_interfaces()) {
        if (iface.name != "lo") {
            return iface.name;
        }
    }

    return "";
}

static std::string shell_escape_config(const std::string& value) {
    std::string escaped;

    for (char c : value) {
        if (c == '\\' || c == '"') {
            escaped.push_back('\\');
        }

        escaped.push_back(c);
    }

    return escaped;
}

static void kill_wpa_supplicant() {
    DIR* dir = opendir("/proc");

    if (!dir) {
        return;
    }

    dirent* entry;

    while ((entry = readdir(dir)) != nullptr) {
        if (!isdigit(entry->d_name[0])) {
            continue;
        }

        std::string pid_text = entry->d_name;
        std::string comm = read_first_line("/proc/" + pid_text + "/comm");

        if (comm == "wpa_supplicant") {
            kill(std::stoi(pid_text), SIGTERM);
        }
    }

    closedir(dir);
    usleep(300000);
}

static void print_tool_status() {
    const char* tools[] = {"ip", "iw", "rfkill", "wpa_supplicant", "wpa_passphrase", "udhcpc", "wget", "ping"};

    shell_print(std::string(BOLD) + "Tools:\n" + RESET);

    for (const char* tool : tools) {
        shell_print("  ");
        shell_print(tool);
        shell_print(": ");
        shell_print(command_exists(tool) ? std::string(GNR) + "ok" + RESET : std::string(RED) + "missing" + RESET);
        shell_print("\n");
    }
}

static void print_interfaces() {
    std::vector<NetInterface> interfaces = list_interfaces();

    shell_print(std::string(BOLD) + "Interfaces:\n" + RESET);

    if (interfaces.empty()) {
        shell_print("  no interfaces found in /sys/class/net\n");
        return;
    }

    for (const NetInterface& iface : interfaces) {
        shell_print("  " + iface.name + "  ");
        shell_print(iface.wifi ? std::string(LIGHT_BLUE) + "wifi" + RESET : "ethernet/other");
        shell_print("  state=");
        shell_print(iface.state.empty() ? "unknown" : iface.state);
        shell_print("  driver=");
        shell_print(iface.driver);
        shell_print("\n");
    }
}

static bool has_ipv4(const std::string& iface) {
    std::vector<std::string> args = {"ip", "-4", "addr", "show"};

    if (!iface.empty()) {
        args.push_back(iface);
    }

    RunResult result = run_command(args);
    return result.output.find(" inet ") != std::string::npos;
}

static bool has_default_route() {
    RunResult result = run_command({"ip", "route"});
    return result.output.find("default ") != std::string::npos;
}

static bool has_dns() {
    std::ifstream file("/etc/resolv.conf");
    std::string line;

    while (std::getline(file, line)) {
        line = trim(line);

        if (line.find("nameserver ") == 0) {
            return true;
        }
    }

    return false;
}

static bool run_network_test(const std::string& iface, bool verbose) {
    bool ok = true;

    if (verbose) {
        print_interfaces();
        shell_print("\n");
    }

    bool ipv4 = has_ipv4(iface);
    bool route = has_default_route();
    bool dns = has_dns();

    shell_print("IPv4 address: ");
    shell_print(ipv4 ? std::string(GNR) + "ok" + RESET : std::string(RED) + "missing" + RESET);
    shell_print("\n");

    shell_print("Default route: ");
    shell_print(route ? std::string(GNR) + "ok" + RESET : std::string(RED) + "missing" + RESET);
    shell_print("\n");

    shell_print("DNS config: ");
    shell_print(dns ? std::string(GNR) + "ok" + RESET : std::string(RED) + "missing" + RESET);
    shell_print("\n");

    ok = ipv4 && route && dns;

    RunResult ping = run_command({"ping", "-c", "1", "-W", "3", "1.1.1.1"});
    shell_print("Ping 1.1.1.1: ");
    shell_print(ping.code == 0 ? std::string(GNR) + "ok" + RESET : std::string(RED) + "failed" + RESET);
    shell_print("\n");

    RunResult wget = run_command({"wget", "-T", "8", "-O", "/tmp/dew-net-test", "http://example.com"});
    shell_print("HTTP test: ");
    shell_print(wget.code == 0 ? std::string(GNR) + "ok" + RESET : std::string(RED) + "failed" + RESET);
    shell_print("\n");

    ok = ok && (ping.code == 0 || wget.code == 0);

    if (verbose) {
        shell_print("\n");
        print_command(iface.empty() ? std::vector<std::string>{"ip", "addr"} : std::vector<std::string>{"ip", "addr", "show", iface});
        print_command({"ip", "route"});
    }

    return ok;
}

static void scan_wifi(const std::string& requested_iface) {
    std::string iface = requested_iface.empty() ? first_wifi_interface() : requested_iface;

    if (iface.empty()) {
        shell_print(std::string(RED) + "wifi-scan: no Wi-Fi interface found\n" + RESET);
        shell_print("In QEMU this is normal unless you pass a real USB Wi-Fi adapter.\n");
        return;
    }

    if (!command_exists("iw")) {
        shell_print("wifi-scan: missing iw\n");
        return;
    }

    run_command({"rfkill", "unblock", "all"});
    run_command({"ip", "link", "set", iface, "up"});

    shell_print("Scanning on " + iface + "...\n");
    RunResult scan = run_command({"iw", "dev", iface, "scan"});

    if (scan.code != 0) {
        shell_print(std::string(RED) + "scan failed\n" + RESET);
        shell_print(scan.output.empty() ? "No output from iw.\n" : scan.output);
        shell_print("Possible reasons: wrong interface, rfkill block, missing firmware, missing driver, or no real Wi-Fi device.\n");
        return;
    }

    struct Network {
        std::string ssid;
        std::string signal;
        std::string security = "open";
    };

    std::vector<Network> networks;
    Network current;

    auto flush_current = [&]() {
        if (!current.ssid.empty()) {
            networks.push_back(current);
        }

        current = Network{};
    };

    for (const std::string& raw_line : split_lines(scan.output)) {
        std::string line = trim(raw_line);

        if (line.find("BSS ") == 0) {
            flush_current();
        } else if (line.find("SSID: ") == 0) {
            current.ssid = line.substr(6);
        } else if (line.find("signal: ") == 0) {
            current.signal = line.substr(8);
        } else if (line.find("RSN:") == 0) {
            current.security = "WPA2/RSN";
        } else if (line.find("WPA:") == 0) {
            current.security = "WPA";
        } else if (line.find("capability:") == 0 && line.find("Privacy") != std::string::npos) {
            current.security = "protected";
        }
    }

    flush_current();

    if (networks.empty()) {
        shell_print("No visible networks found.\n");
        return;
    }

    shell_print(std::string(BOLD) + "SSID                              SIGNAL        SECURITY\n" + RESET);

    for (const Network& network : networks) {
        std::string ssid = network.ssid;
        std::string signal = network.signal;

        if (ssid.size() > 32) {
            ssid = ssid.substr(0, 29) + "...";
        }

        if (signal.size() > 12) {
            signal = signal.substr(0, 12);
        }

        ssid.append(34 - ssid.size(), ' ');
        signal.append(14 - signal.size(), ' ');

        shell_print(ssid + signal + network.security + "\n");
    }
}

static void request_dhcp(const std::string& iface) {
    std::string real_iface = iface.empty() ? first_network_interface() : iface;

    if (real_iface.empty()) {
        shell_print("dhcp: missing interface\n");
        return;
    }

    run_command({"ip", "link", "set", real_iface, "up"});

    if (file_exists("/etc/udhcpc/default.script")) {
        print_command({"udhcpc", "-i", real_iface, "-s", "/etc/udhcpc/default.script", "-q", "-n"});
    } else {
        print_command({"udhcpc", "-i", real_iface, "-q", "-n"});
    }
}

void cmd_wifi_detect(const CommandContext& ctx) {
    (void)ctx;

    print_tool_status();
    shell_print("\n");
    print_interfaces();

    if (command_exists("iw")) {
        shell_print("\n");
        print_command({"iw", "dev"});
    }

    if (command_exists("rfkill")) {
        shell_print("\n");
        print_command({"rfkill", "list"});
    }
}

void cmd_wifi_scan(const CommandContext& ctx) {
    std::string iface = ctx.args.empty() ? "" : ctx.args[0];
    scan_wifi(iface);
}

void cmd_wifi_connect_debug(const CommandContext& ctx) {
    std::string iface = first_wifi_interface();
    std::string ssid;
    std::string pass;

    if (ctx.args.size() == 1) {
        if (file_exists("/sys/class/net/" + ctx.args[0])) {
            iface = ctx.args[0];
        } else {
            ssid = ctx.args[0];
        }
    } else if (ctx.args.size() >= 2) {
        if (file_exists("/sys/class/net/" + ctx.args[0])) {
            iface = ctx.args[0];
            ssid = ctx.args[1];

            if (ctx.args.size() > 2) {
                pass = ctx.args[2];
            }
        } else {
            ssid = ctx.args[0];
            pass = ctx.args[1];
        }
    }

    if (iface.empty()) {
        shell_print("wifi-connect-debug: no Wi-Fi interface found\n");
        return;
    }

    if (!command_exists("wpa_supplicant")) {
        shell_print("wifi-connect-debug: missing wpa_supplicant\n");
        return;
    }

    if (ssid.empty()) {
        disable_raw_mode();
        raw_write("\033[?25h");

        std::cout << "Wi-Fi interface [" << iface << "]: " << std::flush;
        std::string input_iface;
        std::getline(std::cin, input_iface);

        if (!trim(input_iface).empty()) {
            iface = trim(input_iface);
        }

        std::cout << "SSID: " << std::flush;
        std::getline(std::cin, ssid);
        ssid = trim(ssid);

        if (!ssid.empty()) {
            pass = read_password("Password (empty for open network): ");
        }

        enable_raw_mode();
        raw_write("\033[?25l");
    }

    if (ssid.empty()) {
        shell_print("wifi-connect-debug: SSID cannot be empty\n");
        return;
    }

    mkdir("/etc/wpa_supplicant", 0755);
    mkdir("/var/run", 0755);
    mkdir("/var/run/wpa_supplicant", 0755);

    std::ofstream conf("/etc/wpa_supplicant/wpa_supplicant.conf");

    conf << "ctrl_interface=/var/run/wpa_supplicant\n";
    conf << "update_config=1\n\n";
    conf << "network={\n";
    conf << "    ssid=\"" << shell_escape_config(ssid) << "\"\n";
    conf << "    scan_ssid=1\n";

    if (pass.empty()) {
        conf << "    key_mgmt=NONE\n";
    } else {
        conf << "    psk=\"" << shell_escape_config(pass) << "\"\n";
    }

    conf << "}\n";
    conf.close();
    chmod("/etc/wpa_supplicant/wpa_supplicant.conf", 0600);

    shell_print("Wi-Fi connect debug\n");
    shell_print("iface: " + iface + "\n");
    shell_print("ssid: " + ssid + "\n");
    shell_print("log: /tmp/wifi-connect-debug.log\n\n");

    kill_wpa_supplicant();
    print_command({"rfkill", "unblock", "all"});
    print_command({"ip", "link", "set", iface, "up"});
    print_command({"wpa_supplicant", "-B", "-i", iface, "-c", "/etc/wpa_supplicant/wpa_supplicant.conf", "-f", "/tmp/wifi-connect-debug.log", "-d"});

    sleep(5);

    print_command({"iw", "dev", iface, "link"});
    request_dhcp(iface);

    shell_print("\n");
    run_network_test(iface, true);

    shell_print("\nLast wpa_supplicant log lines:\n");
    print_command({"tail", "-n", "40", "/tmp/wifi-connect-debug.log"});
}

void cmd_net_test(const CommandContext& ctx) {
    std::string iface = ctx.args.empty() ? "" : ctx.args[0];
    bool ok = run_network_test(iface, true);

    shell_print("\nNetwork result: ");
    shell_print(ok ? std::string(GNR) + "ok" + RESET : std::string(RED) + "failed" + RESET);
    shell_print("\n");
}

void cmd_network(const CommandContext& ctx) {
    if (ctx.args.empty() || ctx.args[0] == "status") {
        print_tool_status();
        shell_print("\n");
        run_network_test("", true);
        shell_print("\nUsage:\n");
        shell_print("  network detect\n");
        shell_print("  network scan [iface]\n");
        shell_print("  network connect-debug [iface] [ssid] [password]\n");
        shell_print("  network dhcp <iface>\n");
        shell_print("  network test [iface]\n");
        return;
    }

    std::string action = ctx.args[0];
    CommandContext sub = ctx;
    sub.args.erase(sub.args.begin());

    if (action == "detect") {
        cmd_wifi_detect(sub);
    } else if (action == "scan") {
        cmd_wifi_scan(sub);
    } else if (action == "connect-debug") {
        cmd_wifi_connect_debug(sub);
    } else if (action == "dhcp") {
        request_dhcp(sub.args.empty() ? "" : sub.args[0]);
    } else if (action == "test" || action == "network") {
        cmd_net_test(sub);
    } else if (action == "up") {
        if (sub.args.empty()) {
            shell_print("usage: network up <iface>\n");
        } else {
            print_command({"ip", "link", "set", sub.args[0], "up"});
        }
    } else {
        shell_print("network: unknown action: " + action + "\n");
        shell_print("usage: network {detect|scan|connect-debug|dhcp|test|up}\n");
    }
}