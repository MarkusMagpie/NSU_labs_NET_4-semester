#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>

const int DHCP_SERVER_PORT = 67;

int main() {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        std::cerr << "Ошибка создания сокета\n";
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DHCP_SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Ошибка привязки сокета\n";
        close(sock);
        return 0;
    }

    std::cout << "DHCP-сервер запущен...\n";
    char buffer[1024];

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        ssize_t received = recvfrom(sock, buffer, sizeof(buffer)-1, 0, 
                                   (sockaddr*)&client_addr, &client_len);
        if (received <= 0) continue;

        buffer[received] = '\0';
        std::string request(buffer);

        if (request.find("DHCP_DISCOVER ") == 0) {
            std::string mac = request.substr(14);
            std::string response = "DHCP_OFFER 192.168.1.100"; // Пример IP
            sendto(sock, response.c_str(), response.size(), 0,
                  (sockaddr*)&client_addr, client_len);
            std::cout << "Отправлен IP клиенту: 192.168.1.100\n";
        }
    }

    close(sock);
    return 0;
}