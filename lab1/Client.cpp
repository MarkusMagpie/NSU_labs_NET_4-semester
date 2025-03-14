#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>

const int ROUTER_PORT = 5000;

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

void send_ping(int sock, const sockaddr_in& dest_addr, const ClientInfo& client, int seq, const std::string& target_ip) {
    std::string message = "PING " + std::to_string(seq) + 
                        " to " + target_ip + 
                        " from " + client.ip + 
                        " (" + client.mac + ")";
    
    size_t sent = sendto(sock, message.c_str(), message.length(), 0, reinterpret_cast<const sockaddr*>(&dest_addr), sizeof(dest_addr));
    
    if (sent < 0) {
        throw std::runtime_error("Ошибка отправки сообщения");
    }
    
    std::cout << "Отправлено: " << message << "\n";
}

// функция приема ответа
void receive_response(int sock) {
    char buffer[1024];
    sockaddr_in from_addr;
    socklen_t addr_len = sizeof(from_addr);
    
    memset(buffer, 0, sizeof(buffer));
    size_t received = recvfrom(sock, buffer, sizeof(buffer)-1, 0, reinterpret_cast<sockaddr*>(&from_addr), &addr_len);
    
    if (received < 0) {
        std::cout << "Ответ не получен (таймаут).\n";
    } else {
        buffer[received] = '\0';
        std::cout << "Получен ответ: " << buffer << "\n";
    }
}

int main() {
    ClientInfo client{"127.0.0.1", "AA:BB:CC:DD:EE:FF"};
    int sequence = 0;

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
                send_ping(sock, dest_addr, client, sequence, dest_ip);
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
        return -1;
    }

    std::cout << "Клиентский компьютер завершен!\n";
    return 0;
}
