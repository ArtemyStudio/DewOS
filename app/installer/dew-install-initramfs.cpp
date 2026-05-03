#include <iostream>
#include <string>

int main() {
    std::cout << "[DewOS Installer]\n";
    std::cout << "Minimal installer started.\n\n";
    std::cout << "Real installer is not inside initramfs yet.\n";
    std::cout << "For now this only proves that install command works.\n\n";
    std::cout << "Press Enter to return.\n";

    std::string dummy;
    std::getline(std::cin, dummy);

    return 0;
}
