#include <iostream>
#include <cstring>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <arpa/inet.h> // inet_pton - преобразование IP-адреса из строки в бинарный формат
#include <unistd.h> // close для закрытия сокета
#include <map>

const int ROUTER_PORT = 5000;

struct ClientInfo {
    std::string ip;
    std::string mac;
    sockaddr_in address; // адрес для ответа
    time_t last_seen;   // время последней активности
};

// таблица клиентов + мьютекс
std::map<std::string, ClientInfo> clients;
std::mutex clients_mutex;

// очистка неактивных клиентов
void clean_clients() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(60)); // активность проверяется раз в минуту
        const std::lock_guard<std::mutex> lock(clients_mutex); // блокируем доступ к таблице другим потоков (lock_guard круче lock)
        auto now = time(nullptr);
        for (auto it = clients.begin(); it != clients.end();) {
            if (now - it->second.last_seen > 60) {
                std::cout << "Удален неактивный клиент: " << it->first << std::endl;
                it = clients.erase(it);
            } else {
                ++it;
            }
        }
    }
}

// извлечение MAC-адреса из сообщения
std::string parse_mac(const std::string& message) {
    int start = message.find("(") + 1;
    int end = message.find(")");

    return message.substr(start, end - start);
}

// обработка пакета от клиента
void handle_packet(int sock, sockaddr_in client_addr, char* buffer, ssize_t len) {
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    std::string message(buffer, len);

    std::string mac;
    try {
        mac = parse_mac(message);
    } catch (const std::exception& e) {
        std::cout << "Ошибка парсинга MAC от " << client_ip << std::endl;
        return;
    }

    // обновление таблицы клиентов
    {
        std::lock_guard<std::mutex> lock(clients_mutex); // блокируем доступ к таблице другим потоков (синхронизация)
        clients[mac] = {
            client_ip,
            mac,
            client_addr,
            time(nullptr)
        };
    }

    // извлекаем целевой IP 
    // PING [seq] to [target_ip] from [source_ip] ([MAC])
    // PING 0 to 127.0.0.1 from 127.0.0.1 (AA:BB:CC:DD:EE:FF)
    size_t to_pos = message.find("to ");
    size_t from_pos = message.find("from ");
    if (to_pos == std::string::npos || from_pos == std::string::npos) {
        std::cerr << "Некорректный формат сообщения" << std::endl;
        return;
    }
    std::string target_ip = message.substr(to_pos + 3, from_pos - to_pos - 4);

    // поиск целевого клиента
    ClientInfo target;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(clients_mutex); // опять синхронизация
        for (const auto& [_, client] : clients) {
            if (client.ip == target_ip) {
                target = client;
                found = true;
                break;
            }
        }
    }

    // если пакет найден, то отправляем его целевому клиенту
    if (found) {
        sendto(sock, buffer, len, 0, 
              (sockaddr*)&target.address, sizeof(target.address));
        std::cout << "Пакет перенаправлен к " << target_ip << std::endl;
    } else {
        std::cout << "Цель " << target_ip << " недоступна" << std::endl;
    }
}

void run_router(int sock) {
    std::cout << "Маршрутизатор запущен на порту: " << ROUTER_PORT << std::endl;
    
    std::thread(clean_clients).detach(); // detach - запускает поток в фоне (независимо от основного)

    // постоянно принимаем UDP-пакеты
    while (true) {
        char buffer[1024];
        sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        // получаем данные из сокета, переданного параметром:
            // sock - дескриптор сокета
            // buffer - буфер для приема данных
            // sizeof(buffer) - размер буфера
            // 0 - флаги
            // (sockaddr*)&client_addr - указатель на структуру sockaddr_in для хранения адреса отправителя
            // &addr_len - указатель на переменную для хранения размера структуры sockaddr_in
        ssize_t len = recvfrom(sock, buffer, sizeof(buffer), 0,
                              (sockaddr*)&client_addr, &addr_len);
        if (len == -1) {
            std::cerr << "Ошибка приема пакета" << std::endl;
            continue;
        }

        std::thread(handle_packet, sock, client_addr, buffer, len).detach(); // поток функции handle_packet запущен независимо от основного
    }
}

int main() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "Ошибка создания сокета" << std::endl;
        return 1;
    }

    // как в ClientComputer2.cpp я создавал "адрес получателя", так и здесь создал адрес сервера
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET; //  IP адрес к которому будет привязан сокет (IPv4)
    // функция htons возвращает значение в порядке байтов сети TCP/IP.
    server_addr.sin_port = htons(ROUTER_PORT); // номер порта который намерен занять процесс
    // https://man7.org/linux/man-pages/man7/ip.7.html
    server_addr.sin_addr.s_addr = INADDR_ANY; // the socket will be bound to all local interfaces

    // bind привязывает сокет к адресу server_addr
    if (bind(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Ошибка привязки (main)" << std::endl;
        close(sock);
        return -1;
    }

    run_router(sock);

    close(sock);
    return 0;
}