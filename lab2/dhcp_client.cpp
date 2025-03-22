
#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdexcept>
#include <sys/socket.h>
#include <cstdlib>
#include <ctime>

const int ROUTER_PORT = 5000;
const int DHCP_SERVER_PORT = 67;

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
    
    size_t sent = sendto(sock, message.c_str(), message.length(), 0, reinterpret_cast<const sockaddr*>(&dest_addr), sizeof(dest_addr));
    
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
    ssize_t received = recvfrom(sock, buffer, sizeof(buffer)-1, 0, reinterpret_cast<sockaddr*>(&from_addr), &addr_len);
    
    if (received < 0) {
        std::cout << "Ответ не получен (таймаут).\n";
    } else {
        buffer[received] = '\0';
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &from_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
        std::cout << "Получен ответ от " << ip_str << ": " << buffer << "\n";
    }
}

std::string dhcp_handshake(const std::string& mac) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); // UDP сокет
    if (sock < 0) {
        throw std::runtime_error("Ошибка создания DHCP сокета");
    }

    // broadcast: разрешает отправку широковещательных пакетов
    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    // timeout: 15 секунд
    timeval timeout{.tv_sec = 15, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DHCP_SERVER_PORT);
    // inet_pton() преобразует введённую строку IP-адреса ip в числовое представление и сохраняет его в server_addr.sin_addr
    inet_pton(AF_INET, "255.255.255.255", &server_addr.sin_addr);

    // Этап 1: DHCPDISCOVER
    std::string discover_msg = "DHCP_DISCOVER " + mac;
    // sendto() отправляет сообщение через UDP-сокет: 
        //      sock – дескриптор сокета
        //      discover_msg.c_str() – указатель на буфер с данными
        //      discover_msg.length() – размер сообщения
        //      последние параметры – адрес получателя и его размер соответственно
    sendto(sock, discover_msg.c_str(), discover_msg.size(), 0,
          reinterpret_cast<const sockaddr*>(&server_addr), sizeof(server_addr));

    // Получение DHCPOFFER - извлекаю предложенный IP адрес
    char buffer[1024];
    sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    // recvfrom() получает данные из сокета sock, 
    // https://www.opennet.ru/cgi-bin/opennet/man.cgi?topic=recvfrom&category=2
    ssize_t received = recvfrom(sock, buffer, sizeof(buffer)-1, 0,
                               reinterpret_cast<sockaddr*>(&from_addr), &from_len);
    
    if (received <= 0) {
        close(sock);
        throw std::runtime_error("Нет ответа на DHCPDISCOVER");
    }
    buffer[received] = '\0';
    
    // парсинг DHCPOFFER - проверка на корректность, извлечение IP - подстрока после "DHCP_OFFER "
    std::string offer(buffer);
    if (offer.find("DHCP_OFFER ") != 0) {
        close(sock);
        throw std::runtime_error("Неверный ответ DHCPOFFER");
    }
    std::string ip = offer.substr(11);

    // Этап 2: DHCPREQUEST - отправка запроса на использование предложенного IP
    std::string request_msg = "DHCP_REQUEST " + mac + " " + ip;
    sendto(sock, request_msg.c_str(), request_msg.size(), 0,
          reinterpret_cast<const sockaddr*>(&server_addr), sizeof(server_addr));

    // Получение DHCPACK/DHCPNAK
    received = recvfrom(sock, buffer, sizeof(buffer)-1, 0,
                       reinterpret_cast<sockaddr*>(&from_addr), &from_len);

    if (received <= 0) {
        close(sock);
        throw std::runtime_error("Нет ответа на DHCPREQUEST");
    }
    buffer[received] = '\0';

    std::string ack(buffer);
    if (ack.find("DHCP_ACK ") == 0) {
        std::cout << "Получен DHCPACK - успешное использование IP" << std::endl;
        close(sock);
        return ip;
    } 
    else if (ack.find("DHCP_NAK ") == 0) {
        std::cout << "Получен DHCPNAK - IP занят" << std::endl;
        close(sock);
        throw std::runtime_error("Сервер отклонил запрос");
    }
    else {
        close(sock);
        throw std::runtime_error("Неизвестный ответ от сервера");
    }
}

int main() {
    std::srand(std::time(nullptr));
    ClientInfo client{"0.0.0.0", "AA:BB:CC:DD:EE:" + std::to_string(std::rand() % 100)};
    int sequence = 0;

    try {
        std::cout << "Получение IP от DHCP сервера...\n";
        client.ip = dhcp_handshake(client.mac);
        std::cout << "Успешно получен IP: " << client.ip << "\n";
    } catch (const std::exception& e) {
        std::cout << "Ошибка DHCP: " << e.what() << ". Используется 0.0.0.0\n";
    }

    std::cout << "\nDHCP клиент запущен\n"
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
                // формируем адрес получателя (create_destination_address(dest_ip)) - создается структура sockaddr_in для целевого узла
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

    std::cout << "Клиент завершен!\n";
    return EXIT_SUCCESS;
}