#include <iostream>
#include <cstring>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <arpa/inet.h> // inet_pton - преобразование IP-адреса из строки в бинарный формат
#include <unistd.h> // close для закрытия сокета

const int ROUTER_PORT = 5000;

struct ClientInfo {
    std::string ip;
    std::string mac;
    sockaddr_in address; // адрес для ответа
    time_t last_seen;   // время последней активности
};

// таблица клиентов и мьютекс 
std::unordered_map<std::string, ClientInfo> clients;
std::mutex clients_mutex;

// очистка неактивных клиентов
void clean_clients() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(60));
        std::lock_guard<std::mutex> lock(clients_mutex);
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

// извлечение MAC из сообщения
std::string parse_mac(const std::string& message) {
    size_t start = message.find("(") + 1;
    size_t end = message.find(")");
    return message.substr(start, end - start);
}

// обработка пакета
void handle_packet(int sock, sockaddr_in client_addr, char* buffer, ssize_t len) {
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    std::string message(buffer, len);

    // извлекаем MAC
    std::string mac;
    try {
        mac = parse_mac(message);
    } catch (const std::exception& e) {
        std::cerr << "Ошибка парсинга MAC от " << client_ip << std::endl;
        return;
    }

    // обновка таблицы клиентов
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
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
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (const auto& [_, client] : clients) {
            if (client.ip == target_ip) {
                target = client;
                found = true;
                break;
            }
        }
    }

    // пересылка пакета
    if (found) {
        sendto(sock, buffer, len, 0, 
              (sockaddr*)&target.address, sizeof(target.address));
        std::cout << "Пакет перенаправлен к " << target_ip << std::endl;
    } else {
        std::cout << "Цель " << target_ip << " недоступна" << std::endl;
    }
}

// главный цикл маршрутизатора
void run_router(int sock) {
    std::cout << "Маршрутизатор запущен на порту " << ROUTER_PORT << std::endl;
    std::thread(clean_clients).detach();

    while (true) {
        char buffer[1024];
        sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        // получаем данные из сокета, переданного параметром
        ssize_t len = recvfrom(sock, buffer, sizeof(buffer), 0,
                              (sockaddr*)&client_addr, &addr_len);
        if (len < 0) {
            std::cerr << "Ошибка приема пакета" << std::endl;
            continue;
        }

        std::thread(handle_packet, sock, client_addr, buffer, len).detach();
    }
}

int main() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "Ошибка создания сокета" << std::endl;
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET; //  IP адрес к которому будет привязан сокет (IPv4)
    // функция htons возвращает значение в порядке байтов сети TCP/IP.
    server_addr.sin_port = htons(ROUTER_PORT); // номер порта который намерен занять процесс
    // https://man7.org/linux/man-pages/man7/ip.7.html
    server_addr.sin_addr.s_addr = INADDR_ANY; // the socket will be bound to all local interfaces

    if (bind(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Ошибка привязки" << std::endl;
        close(sock);
        return 1;
    }

    run_router(sock);

    close(sock);
    return 0;
}