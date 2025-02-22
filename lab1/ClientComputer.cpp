#include <iostream>
#include <cstring>      // memset
#include <sys/socket.h> // для сокетов
#include <arpa/inet.h>  // inet_pton - преобразование IP-адреса из строки в бинарный формат
#include <netinet/in.h> // для sockaddr_in
#include <unistd.h>     // close для закрытия сокета

// это порт на который отправляются запросы (маршрутизатор должен слушать на этом порту)
const int ROUTER_PORT = 5000;

int main() {
    // MAC и IP-адрес клиента
    std::string clientIP = "192.168.1.2";
    std::string clientMAC = "AA:BB:CC:DD:EE:FF";

    std::cout << "Клиентский компьютер запущен.\n";
    std::cout << "IP-адрес: " << clientIP << "\n";
    std::cout << "MAC-адрес: " << clientMAC << "\n\n";

    // Создаём UDP-сокет
    // AF_INET - IPv4, SOCK_DGRAM - UDP
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        std::cout << "Ошибка создания сокета" << std::endl;
        return 0;
    }

    // устанавливаем таймаут на операцию приема сообщений (2 секунды)
    // setsockopt() задаёт таймаут в 2 сек для операций чтения с сокета
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) < 0) {
        std::cout << "Ошибка установки таймаута" << std::endl;
        close(sock);
        return 0;
    }

    int sequence = 0; // нумерация запросов

    // в цикле запрашиваем у пользователя IP-адрес узла, 
    // формируем сообщение ping с номером запроса,
    // отправляет его на указанный адрес (маршрутизатор, работающий на порту 5000);
    // прием ответа: ожидает ответа от узла; если ответ получен — выводит его, если нет — сообщает о таймауте.
    while (true) {
        std::string destIP;
        std::cout << "Введите IP-адрес узла для отправки ping (или 'exit' для выхода): ";
        std::getline(std::cin, destIP);
        if (destIP == "exit")
            break;

        // Формируем адрес получателя (маршрутизатора)
        struct sockaddr_in destAddr; // переменная для хранения адреса узла назначения
        memset(&destAddr, 0, sizeof(destAddr));
        destAddr.sin_family = AF_INET; // используем IPv4
        destAddr.sin_port = htons(ROUTER_PORT);
        // inet_pton() преобразует введённую строку IP-адреса destIP в числовое представление и сохраняет его в destAddr.sin_addr
        if (inet_pton(AF_INET, destIP.c_str(), &destAddr.sin_addr) <= 0) {
            std::cerr << "Неверный формат IP-адреса: " << destIP << "\n";
            continue;
        }

        // Формируем сообщение ping
        std::string message = "PING " + std::to_string(sequence) +
                              " from " + clientIP +
                              " (" + clientMAC + ")";
        ssize_t sentBytes = sendto(sock, message.c_str(), message.length(), 0,
                                   (struct sockaddr*)&destAddr, sizeof(destAddr));
        // sendto() отправляет сообщение через UDP-сокет: 
        //      sock – дескриптор сокета
        //      message.c_str() – указатель на буфер с данными
        //      message.length() – размер сообщения
        //      последние параметры – адрес получателя и его размер.
        if (sentBytes < 0) {
            std::cout << "Ошибка отправки сообщения" << std::endl;
            continue;
        }

        std::cout << "Отправлено: " << message << "\n";

        // Ожидаем ответа
        char buffer[1024];
        memset(buffer, 0, sizeof(buffer));
        struct sockaddr_in fromAddr;
        socklen_t fromLen = sizeof(fromAddr);
        // recvfrom() получает данные из UDP-сокета
        ssize_t recvBytes = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                                     (struct sockaddr*)&fromAddr, &fromLen);
        if (recvBytes < 0) {
            std::cout << "Ответ не получен (таймаут).\n";
        } else {
            buffer[recvBytes] = '\0'; // завершающий ноль
            std::cout << "Получен ответ: " << buffer << "\n";
        }
        std::cout << "-------------------------------------\n";
        ++sequence;
    }

    close(sock);
    std::cout << "Программа завершена.\n";
    return 0;
}
