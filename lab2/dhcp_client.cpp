#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdexcept>
#include <sys/socket.h>

const int ROUTER_PORT = 5000;
const int DHCP_SERVER_PORT = 67;
const int DHCP_CLIENT_PORT = 68;

struct ClientInfo {
    std::string ip;
    std::string mac;
};

int create_udp_socket() {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        throw std::runtime_error("Ошибка создания сокета");
    }
    
    timeval timeout{.tv_sec = 15, .tv_usec = 0};
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        close(sock);
        throw std::runtime_error("Ошибка установки таймаута");
    }
    
    return sock;
}

sockaddr_in create_destination_address(const std::string& ip) {
    sockaddr_in addr{};
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ROUTER_PORT);
    
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
        throw std::invalid_argument("Неверный формат IP-адреса: " + ip);
    }
    
    return addr;
}

void send_ping(int sock, const sockaddr_in& dest_addr, const ClientInfo& client, int seq) {
    std::string message = "PING " + std::to_string(seq) + 
                        " from " + client.ip + 
                        " (" + client.mac + ")";
    
    size_t sent = sendto(sock, 
                         message.c_str(), 
                         message.length(), 
                         0,
                         reinterpret_cast<const sockaddr*>(&dest_addr),
                         sizeof(dest_addr));
    
    if (sent < 0) {
        throw std::runtime_error("Ошибка отправки сообщения");
    }
    
    std::cout << "Отправлено: " << message << "\n";
}

void receive_response(int sock) {
    char buffer[1024];
    sockaddr_in from_addr;
    socklen_t addr_len = sizeof(from_addr);
    
    memset(buffer, 0, sizeof(buffer));
    size_t received = recvfrom(sock, 
                               buffer, 
                               sizeof(buffer)-1, 
                               0,
                               reinterpret_cast<sockaddr*>(&from_addr),
                               &addr_len);
    
    if (received < 0) {
        std::cout << "Ответ не получен (таймаут).\n";
    } else {
        buffer[received] = '\0';
        std::cout << "Получен ответ: " << buffer << "\n";
    }
}

std::string get_dhcp_ip(const std::string& mac) {
    // UDP-сокет для обмена данными с DHCP-сервером
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); // AF_INET - используем IPv4; SOCK_DGRAM — тип сокета для UDP
    if (sock < 0) {
        throw std::runtime_error("Ошибка функции socket при создании DHCP сокета");
    }

    // включаем возможность отправки широковещательных пакетов (адрес 255.255.255.255).
    int broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        close(sock);
        throw std::runtime_error("Ошибка настройки широковещания");
    }

    // таймер чтобы клиент не завис навсегда, если сервер недоступен
    timeval timeout{.tv_sec = 5, .tv_usec = 0};
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        close(sock);
        throw std::runtime_error("Ошибка установки таймаута DHCP");
    }

    // указываем что отправлять запросы на широковещательный адрес для сервера 
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DHCP_SERVER_PORT);
    inet_pton(AF_INET, "255.255.255.255", &server_addr.sin_addr);

    // формируем и отправляем discover на сервер
    std::string request = "DHCP_DISCOVER " + mac;
    ssize_t sent = sendto(sock, request.c_str(), request.size(), 
                         0, reinterpret_cast<const sockaddr*>(&server_addr), sizeof(server_addr));
    if (sent < 0) {
        close(sock);
        throw std::runtime_error("Ошибка отправки DHCP запроса");
    }

    // ждем ответа от сервера 5 секунд
    char buffer[1024] = {0};
    sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    ssize_t received = recvfrom(sock, buffer, sizeof(buffer)-1, 
                                0, reinterpret_cast<sockaddr*>(&from_addr), &from_len);
    if (received <= 0) {
        close(sock);
        throw std::runtime_error("Не получен ответ от DHCP сервера");
    }
    buffer[received] = '\0';

    // проверяю что response начинается с DHCP_OFFER; извлекаем ip-адрес 
    std::string response(buffer);
    std::string prefix = "DHCP_OFFER ";
    if (response.find(prefix) != 0) {
        close(sock);
        throw std::runtime_error("Неверный ответ от DHCP");
    }
    std::string ip = response.substr(prefix.length());

    // валидируем ip адрес полученный от сервера - проверяем, что строка является IPv4-адресом
    sockaddr_in test_addr{};
    if (inet_pton(AF_INET, ip.c_str(), &test_addr.sin_addr) <= 0) {
        close(sock);
        throw std::runtime_error("Неверный IP от DHCP");
    }

    close(sock);
    return ip;
}

int main() {
    ClientInfo client{"0.0.0.0", "AA:BB:CC:DD:EE:FF"};
    int sequence = 0;

    try {
        std::cout << "Получение IP от DHCP сервера...\n";
        client.ip = get_dhcp_ip(client.mac);
        std::cout << "Успешно получен IP: " << client.ip << "\n";
    } catch (const std::exception& e) {
        std::cout << "Ошибка DHCP: " << e.what() << ". Используется 0.0.0.0\n";
    }

    std::cout << "Клиентский компьютер запущен\n"
              << "IP: " << client.ip << "\n"
              << "MAC: " << client.mac << "\n\n";

    try {
        int sock = create_udp_socket();
        
        while(true) {
            std::string dest_ip;
            std::cout << "Введите IP-адрес узла (или 'exit'): ";
            std::getline(std::cin, dest_ip);
            
            if (dest_ip == "exit" || dest_ip == "q") break;

            try {
                auto dest_addr = create_destination_address(dest_ip);
                send_ping(sock, dest_addr, client, sequence);
                receive_response(sock);
                sequence++;
            }
            catch(const std::exception& e) {
                std::cerr << "Ошибка: " << e.what() << "\n";
            }
            
            std::cout << "-------------------------------------\n";
        }
        
        close(sock);
    }
    catch(const std::exception& e) {
        std::cout << "Ошибка в цикле: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "Клиентский компьютер завершен!\n";
    return EXIT_SUCCESS;
}