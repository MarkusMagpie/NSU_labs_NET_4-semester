#include <iostream>
#include <string>
#include <fstream>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// регистрация в DNS-сервере
/*
делаю сокет UDP для отправки запроса  
заполняю структуру sockaddr_in чтобы привязать сокет к DNS-серверу
формирую сообщение вида REGISTER домен ip 
отправляю его DNS серверу
закрываю сокет (соединение между клиентом и DNS-сервером)
*/
void registerInDNS(std::string& dnsServerInfo, 
                  std::string& domain, 
                  std::string& clientIP, 
                  int clientPort) {
    size_t space = dnsServerInfo.find(' ');
    if(space == std::string::npos) {
        std::cout << "ошибка при парсинге информации о DNS 1\n";
        return;
    }

    size_t colon = dnsServerInfo.find(':', space);
    if(colon == std::string::npos) {
        std::cout << "ошибка при парсинге информации о DNS 2\n";
        return;
    }
    
    std::string ip = dnsServerInfo.substr(space + 1, colon - space - 1);
    int dnsPort = std::stoi(dnsServerInfo.substr(colon + 1));

    // std::cout << "ip: " << ip << " port: " << dnsPort << std::endl;
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    // настройка адреса DNS сервера
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET; // IPv4
    serverAddr.sin_port = htons(dnsPort);
    // https://www.opennet.ru/man.shtml?topic=inet_pton&category=3&russian=0
    inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr); // преобразование IP-адреса в бинарный формат

    std::string message = "REGISTER " + domain + " " + clientIP + ":" + std::to_string(clientPort);
    sendto(sock, message.c_str(), message.size(), 0, (sockaddr*)&serverAddr, sizeof(serverAddr));
    
    close(sock);
}

/*
принцип работы
    1 создание UDP-сокета для общения с DNS-сервером
    2 формирование запроса в формате "QUERY <доменное_имя>" (доменное_имя получаю из GET запроса)
    3 отправка запроса на DNS-сервер
    4 ожидание и обрабатка ответа
*/
std::string queryDNS(const std::string& dnsServerIP, std::string& domain) {
    // 1
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(5353);
    inet_pton(AF_INET, dnsServerIP.c_str(), &serverAddr.sin_addr);

    // 2
    std::string message = "QUERY " + domain;
    
    // 3
    sendto(sock, message.c_str(), message.size(), 0, (sockaddr*)&serverAddr, sizeof(serverAddr));

    // 4
    char buffer[1024];
    socklen_t len = sizeof(serverAddr);
    ssize_t n = recvfrom(sock, buffer, sizeof(buffer)-1, 0, (sockaddr*)&serverAddr, &len);
    close(sock);

    if(n <= 0) return "";
    
    buffer[n] = '\0';
    return std::string(buffer);
}

/*
приницип работы:
    1 парсинг строки "IP:PORT" на составляющие
    2 установка TCP-соединения с целевым сервером (TCP сокет + настройка адреса целевого сервера + соединение <=> connect)
    3 отправка HTTP GET-запроса                       (!!! в тетради покажи как формирует + что в гет запросе нету тела запроса)
    4 принимание и возвращени ответа
*/
std::string getHTML(std::string& ip_and_port) {
    // 1
    size_t colon_pos = ip_and_port.find(':');
    if(colon_pos == std::string::npos) {
        return "Не удалось обработать IP-адрес и порт домена. ПРОВЕРЬ ДОМЕННОЕ ИМЯ!!!";
    }

    std::string ip = ip_and_port.substr(0, colon_pos);
    int port = std::stoi(ip_and_port.substr(colon_pos + 1));

    // 2
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr);

    if(connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        return "Не удалось установить соединение";
    }

    // 3
    std::string request = "GET / HTTP/1.1\r\n" 
                          "Host: " + ip + "\r\n" 
                          "\r\n";
    send(sock, request.c_str(), request.size(), 0);

    // 4
    char buffer[4096];
    ssize_t n = recv(sock, buffer, sizeof(buffer)-1, 0);
    close(sock);

    if(n <= 0) return "";
    
    buffer[n] = '\0';
    return std::string(buffer);
}

/*
принцип работы:
    1 создание UDP-сокета для отправки/приема широковещательных пакетов
    2 разрешение отправки широковещательных пакетов через сокет sock
    3 формирование и отправка запроса "DISCOVER" 
    4 настройка таймаута на прием ответа
    5 прием ответа от DNS-сервера
*/
std::string discoverDNSServer() {
    // 1
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    
    // 2
    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    /*
    настройка адреса для широковещательной рассылки:
        семейство адресов: IPv4 (AF_INET)
        порт DNS-сервера: 5353 (мой порт сервера dns_server.cpp - global variable PORT)
        адрес рассылки: INADDR_BROADCAST (255.255.255.255)
    */
    sockaddr_in broadcastAddr{};
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(5353);
    broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST;

    // 3
    std::string message = "DISCOVER";
    sendto(sock, message.c_str(), message.size(), 0, (sockaddr*)&broadcastAddr, sizeof(broadcastAddr));

    // 4
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // 5
    char buffer[1024];
    sockaddr_in responseAddr{};
    socklen_t addrLen = sizeof(responseAddr);
    ssize_t n = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&responseAddr, &addrLen); // sizeof(buffer) - 1 для нулевого символа
    
    close(sock);

    if(n > 0) {
        buffer[n] = '\0';
        return std::string(buffer);
    }

    return "";
}

/*
создаю сокет TCP SOCK_STREAM 
создаю структуру sockaddr_in
привязываю сокет к порту 
начинаю слушать сокет
    принимаю соединение
    отправляю ответ в виде HTML страницы
    закрываю соединение
*/
void runWebServer(const std::string& htmlContent, int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    // настройка параметров сервера
    sockaddr_in address{};
    address.sin_family = AF_INET; // IPv4
    address.sin_addr.s_addr = INADDR_ANY; // принимаем соединения от любого интерфейса
    address.sin_port = htons(port);

    bind(server_fd, (sockaddr*)&address, sizeof(address)); // привязываем сокет к порту
    listen(server_fd, 5); // начинаем слушать сокет (максимум 5 соединений одновременно)

    while(true) {
        sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);

        // примнимаем входящее соединение
        int clientSocket = accept(server_fd, 
                                (sockaddr*)&clientAddr, 
                                &clientLen);
        
        // http ответ
        std::string response = 
                            "HTTP/1.1 200 OK\r\n"           // строка статус 
                            "Content-Type: text/html\r\n"   // заголовок типа содержимого
                            "\r\n" +                        // конец заголовка 
                            htmlContent;                    // тело ответа
        
        send(clientSocket, response.c_str(), response.size(), 0); // ответ отправляется клиенту
        close(clientSocket); 
    }
}

int main(int argc, char* argv[]) {
    if(argc != 4) {
        std::cout << "параметры!!! пример: ./client example.com index.html 8080" << std::endl;
        return 0;
    }

    // 0 - обнаружение DNS-сервера (задача 3.4)
    std::cout << "Поиск DNS-сервера в сети...\n";
    std::string dnsInfo = discoverDNSServer();
    
    if(dnsInfo.empty() || dnsInfo.find("DNS_SERVER") == std::string::npos) {
        std::cout << "Ошибка: DNS-сервер не найден!\n";
        return 0;
    }

    // 1 - регистрация в DNS
    std::string domain = argv[1];
    std::string clientIP = "127.0.0.1";
    int port = std::stoi(argv[3]);

    registerInDNS(dnsInfo, domain, clientIP, port);

    // 2 - загрузка HTML-контента
    std::string filename = argv[2];
    std::ifstream file(filename);

    // если файл не существует - создаем базовый шаблон
    if (!file.is_open()) {
        std::ofstream new_file(filename);
        if (new_file) {
            std::string default_html = 
                "<!DOCTYPE html>\n"
                "<html>\n"
                "<head>\n"
                "    <title>" + domain + "</title>\n"
                "</head>\n"
                "<body>\n"
                "    <h1>Welcome to " + domain + "!</h1>\n"
                "</body>\n"
                "</html>";
            new_file << default_html;
            new_file.close();
            std::cout << "Создан новый файл: " << filename << std::endl;
        } else {
            std::cout << "ошибка создания файла: " << filename << std::endl;
            return 1;
        }
        file.open(filename); // повторно открываем для чтения
    } else {
        std::cout << "Загружен существующий  файл: " << filename << std::endl;
    }

    std::string htmlContent((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    file.close();

    // 3 - запуск веб-сервера в отдельном потоке
    std::thread webServer(runWebServer, htmlContent, port);
    webServer.detach();

    std::cout << "Запущен клиентский узел. Домен: " << domain 
             << ", IP: " << clientIP << ", порт: " << port << std::endl;
    
    // 4 - интерфейс пользователя запускается в основном потоке
    std::cout << "\nДоступные команды:\n"
              << "GET <domain> - получить страницу узла\n"
              << "EXIT - выход\n\n";

    while(true) {
        std::cout << "> ";
        std::string command;
        std::getline(std::cin, command);

        if(command.substr(0, 4) == "GET ") {
            std::string targetDomain = command.substr(4);
            
            // DNS-запрос для получения IP и порта домена
            std::string ip_port = queryDNS(argv[1], targetDomain);
            
            if(ip_port.empty() || ip_port == "NOT FOUND") {
                std::cout << "Домен не найден!\n";
                continue;
            }

            // получение HTML-страницы по IP:порт
            std::string html = getHTML(ip_port);
            std::cout << "\nОтвет от " << targetDomain << ":\n" << html << "\n\n";
        } else if(command == "EXIT" || command == "exit" || command == "q") {
            break;
        } else {
            std::cout << "Неизвестная команда\n";
        }
    }

    return 0;
}