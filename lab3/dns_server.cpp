#include <iostream>
#include <sstream>
#include <string>
#include <cstring> // memset
#include <sys/socket.h> // работы с сокетами
#include <netinet/in.h> // sockaddr_in
#include <arpa/inet.h> // inet_ntoa
#include <unistd.h> // close
#include <map>

// порт сервера (типа 5353 но для использования 53 нужны привилегии) и размер буфера
constexpr int PORT = 5353;
constexpr int BUFFER_SIZE = 1024;

// принимает строку запроса и ссылку на контейнер с зарегистрированными доменами и IP-адресами
// возвращает строку которая будет ответом клиенту
std::string processRequest(const std::string& request, std::map<std::string, std::string>& dnsRecords, int sockfd) {
    std::istringstream iss(request);
    std::string command;
    iss >> command; // извлекаю команду из запроса request
    std::string response;
    
    /*
    обработка команды регистрации:  REGISTER <domain> <ip>
    возвращает                      OK: <domain> зарегистрирован с IP <ip>

    обработка команды запроса:      QUERY <domain>
    возвращает                      <ip>

    обработка команды обнаружения:  DISCOVER
    возвращает                      DNS_SERVER <ip>:<port>
    */
    if (command == "REGISTER") {
        std::string domain, ip_port;
        iss >> domain >> ip_port;
        size_t pos = ip_port.find(":");
        if (pos == std::string::npos) {
            response = "ERROR: Неверный формат. Используйте: REGISTER <domain> <ip:port>";
        } else {
            std::string ip = ip_port.substr(0, pos);
            int port;
            try {
                port = std::stoi(ip_port.substr(pos + 1));
            } catch (const std::exception& e) {
                response = "ERROR: Неверный формат порта.";
            }
            if (domain.empty() || ip.empty()) {
                response = "ERROR: Неверный формат. Используйте: REGISTER <domain> <ip:port>";
            } else {
                dnsRecords[domain] = ip + ":" + std::to_string(port);
                std::cout << domain << " зарегистрирован с IP " << ip << " и портом " << std::to_string(port) << std::endl;
                response = "OK: " + domain + " зарегистрирован с IP " + ip + " и портом " + std::to_string(port);
            }
        }
    } else if (command == "QUERY") {
        std::string domain;
        iss >> domain;
        if (domain.empty()) {
            response = "ERROR: Неверный формат. Используйте: QUERY <domain>";
        } else {
            auto it = dnsRecords.find(domain);
            if (it != dnsRecords.end()) {
                response = it->second;
            } else {
                response = "NOT FOUND";
            }
        }
    } else if (command == "DISCOVER") {
        sockaddr_in serverAddr{};
        socklen_t addrLen = sizeof(serverAddr);
        
        // получаем информацию о сокете сервера (пишем в serverAddr)
        if(getsockname(sockfd, (sockaddr*)&serverAddr, &addrLen) < 0) {
            response = "ERROR: Не удалось получить информацию о сервере";
        } else {
            char myIP[INET_ADDRSTRLEN]; // буфер для строкового представления ip (здесь лежит IP-адрес сервера после inet_ntop)
            // преобразуем бинарный формат ip-адреса (из serverAddr.sin_addr) в читаемую строку
            inet_ntop(AF_INET, &(serverAddr.sin_addr), myIP, INET_ADDRSTRLEN);
            response = "DNS_SERVER " + std::string(myIP) + ":" + std::to_string(PORT);
        }
    } else {
        response = "ERROR: Неизвестная команда. Используйте REGISTER или QUERY";
    }
    
    return response + "\n> ";
}

int createSocket() {
    /*
    socket() создает сокет, возвращает дескриптор сокета
        __domain - AF_INET - протокол IPv4
        __type - SOCK_DGRAM - UDP
        __protocol - 0 - выбирается автоматически
    */
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cout << "Ошибка при создании сокета" << std::endl;
    }
    return sockfd;
}

// привязки сокета к указанному порту
bool bindSocket(int sockfd) {
    int opt = 1;
    /*
    setsockopt() устанавливает опцию для сокета
        int fd - sockfd - дескриптор сокета
        int level - SOL_SOCKET - устанавливает опцию для сокета
        int optname - SO_REUSEADDR - позволяет повторно использовать порт
        const void *optval - &opt - указатель на опцию
        socklen_t optlen - sizeof(opt) - размер опции opt
    */
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        std::cout << "ошибка setsockopt: " << strerror(errno) << std::endl;
        return false;
    }

    sockaddr_in servAddr; // стркутура, описывает сокет для работы с протоколами IPv4
    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET; //  IP адрес к которому будет привязан сокет (IPv4)
    servAddr.sin_addr.s_addr = INADDR_ANY;
    // функция htons возвращает значение в порядке байтов сети TCP/IP.
    servAddr.sin_port = htons(PORT); // номер порта к которому будет привязан сокет

    if (bind(sockfd, (sockaddr*)&servAddr, sizeof(servAddr)) < 0) {
        std::cout << "ошибка bind: " << strerror(errno) << std::endl;
        return false;
    }
    return true;
}

// основной цикл сервера: приём и обработка запросов
void runServer(int sockfd, std::map<std::string, std::string>& dnsRecords) {
    int broadcast = 1;

    /*
    sockfd:             дескриптор сокета, который настраиваем
    SOL_SOCKET:         уровень настройки (то есть это уровень сокета)
    SO_BROADCAST:       опция для разрешения широковещания
    &broadcast:         указатель на само значение опции
    sizeof(broadcast):  размер переменной с значением
    */
    setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    while (true) {
        char buffer[BUFFER_SIZE];
        memset(buffer, 0, BUFFER_SIZE); // очистка 
        sockaddr_in clientAddr; // структура для хранения адреса клиента
        socklen_t clientLen = sizeof(clientAddr);

        /*
        recfvrom() получает сообщение от клиента через сокет:
            sockfd - дескриптор сокета
            buffer - буфер для данными (изначально пустой)
            BUFFER_SIZE - 1 - сколько байт считать из буффера (-1 чтобы было место для завершения строки)
            0 - флаги
            (sockaddr*)&clientAddr - адрес получателя
            &clientLen - его размер
        */
        int recvLen = recvfrom(sockfd, 
                               buffer, 
                               BUFFER_SIZE - 1, 
                               0, 
                               (sockaddr*)&clientAddr, 
                               &clientLen);
        if (recvLen < 0) {
            std::cout << "ошибка при получении данных от клиента" << std::endl;
            continue;
        }
        buffer[recvLen] = '\0'; // гарант завершения строки

        std::string request(buffer);
        std::string response = processRequest(request, dnsRecords, sockfd);

        /*
        sendto() отправляет сообщение клиенту через сокет: 
            sockfd - дескриптор сокета
            response.c_str() - указатель на буфер с данными
            response.length() - размер сообщения
            clientAddr - адрес получателя 
            clientLen - его размер соответственно
        */
        int sent = sendto(sockfd, 
                          response.c_str(), 
                          response.size(), 
                          0, 
                          (sockaddr*)&clientAddr, 
                          clientLen);
        if (sent < 0) {
            std::cout << "ошибка при отправке данных клиенту" << std::endl;
        }
    }
}

int main() {
    // ассоциативный контейнер пар (домен, IP:порт)
    std::map<std::string, std::string> dnsRecords;

    int sockfd = createSocket();
    if (sockfd < 0)
        return 0;

    if (!bindSocket(sockfd)) {
        close(sockfd);
        return 0;
    }

    std::cout << "DNS-сервер запущен на порту: " << PORT << std::endl;
    
    runServer(sockfd, dnsRecords);

    close(sockfd);
    return 0;
}
