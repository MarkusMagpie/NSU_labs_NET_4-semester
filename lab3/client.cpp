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
void registerInDNS(const std::string& dnsServerIP, 
                  const std::string& domain, 
                  const std::string& clientIP) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET; // IPv4
    serverAddr.sin_port = htons(5353); // порт DNS сервера
    // https://www.opennet.ru/man.shtml?topic=inet_pton&category=3&russian=0
    inet_pton(AF_INET, dnsServerIP.c_str(), &serverAddr.sin_addr); // преобразование IP-адреса в бинарный формат

    std::string message = "REGISTER " + domain + " " + clientIP;
    sendto(sock, message.c_str(), message.size(), 0,
          (sockaddr*)&serverAddr, sizeof(serverAddr));
    
    close(sock);
}

// простейший веб-сервер
/*
создаю сокет TCP SOCK_STREAM 
создаю структуру sockaddr_in
привязываю сокет к порту 8080
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
        
        // здесь формирую http ответ
        std::string response = 
                            "HTTP/1.1 200 OK\r\n"           // статус ответа
                            "Content-Type: text/html\r\n"   // заголовок типа содержимого
                            "\r\n" +                        // конец заголовка 
                            htmlContent;                    // тело ответа
        
        send(clientSocket, response.c_str(), response.size(), 0); // ответ отправляется клиенту
        close(clientSocket); 
    }
}

int main(int argc, char* argv[]) {
    if(argc != 4) {
        std::cout << "параметры!!! пример: ./client 1.4.8.8 example.com index.html" << std::endl;
        return 0;
    }

    // 1 - регистрация в DNS
    std::string dnsIP = argv[1];
    std::string domain = argv[2];
    std::string clientIP = "127.0.0.1";

    registerInDNS(dnsIP, domain, clientIP);

    // 2 - загрузка HTML-контента
    std::string filename = argv[3];
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
            std::cerr << "Ошибка создания файла: " << filename << std::endl;
            return 1;
        }
        file.open(filename); // повторно открываем для чтения
    }

    std::string htmlContent((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    file.close();

    // 3 - запуск веб-сервера
    std::thread webServer(runWebServer, htmlContent, 8080);
    webServer.detach();

    std::cout << "Запущен клиентский узел. Домен: " << domain 
             << ", IP: " << clientIP << std::endl;
    
    // беск цикл для поддержания работы
    while(true) {
        std::this_thread::sleep_for(std::chrono::hours(1));
    }
}