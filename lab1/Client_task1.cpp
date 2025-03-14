#include <iostream>
#include <cstring>
#include <arpa/inet.h> // inet_pton - преобразование IP-адреса из строки в бинарный формат
#include <unistd.h> // close для закрытия сокета

const int ROUTER_PORT = 5000;

// информация о клиенте
struct ClientInfo {
    std::string ip;
    std::string mac;
};

// функция создания и настройки UDP-сокета
// AF_INET - IPv4, socket type: SOCK_DGRAM - UDP https://www.ibm.com/docs/pl/aix/7.1?topic=protocols-socket-types
int create_udp_socket() {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        throw std::runtime_error("Ошибка создания сокета");
    }
    
    // устанавливаем таймаут на операцию приема сообщений
    // setsockopt() задаёт таймаут в сколько-то сек для операций чтения с сокета
    timeval timeout{.tv_sec = 15, .tv_usec = 0};
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        close(sock);
        throw std::runtime_error("Ошибка установки таймаута");
    }
    
    return sock;
}

// создание адреса получателя
sockaddr_in create_destination_address(const std::string& ip) {
    // https://www.opennet.ru/docs/RUS/socket/node4.html
    sockaddr_in addr{}; // стркутура, описывает сокет для работы с протоколами IP
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; //  IP адрес к которому будет привязан сокет (IPv4)
    // функция htons возвращает значение в порядке байтов сети TCP/IP.
    addr.sin_port = htons(ROUTER_PORT); // номер порта который намерен занять процесс
    
    // inet_pton() преобразует введённую строку IP-адреса ip в числовое представление и сохраняет его в addr.sin_addr
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
        throw std::invalid_argument("Неверный формат IP-адреса: " + ip);
    }
    
    return addr;
}

// функция отправки ping-сообщения
void send_ping(int sock, const sockaddr_in& dest_addr, const ClientInfo& client, int seq) {
    std::string message = "PING " + std::to_string(seq) + 
                        " from " + client.ip + 
                        " (" + client.mac + ")" +
                        " to " + inet_ntoa(dest_addr.sin_addr) + "\n";
    
    // sendto() отправляет сообщение через UDP-сокет: 
        //      sock – дескриптор сокета
        //      message.c_str() – указатель на буфер с данными
        //      message.length() – размер сообщения
        //      последние параметры – адрес получателя и его размер соответственно
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

// функция приема ответа
void receive_response(int sock) {
    char buffer[1024];
    sockaddr_in from_addr;
    socklen_t addr_len = sizeof(from_addr);
    
    memset(buffer, 0, sizeof(buffer));
    // recvfrom() получает данные из сокета sock, 
    // https://www.opennet.ru/cgi-bin/opennet/man.cgi?topic=recvfrom&category=2
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
        std::cout << "Получено: " << buffer << "\n";
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
        return -1;
    }

    std::cout << "Клиентский компьютер завершен!\n";
    return 0;
}

// g++ -o client Client_task1.cpp && ./client