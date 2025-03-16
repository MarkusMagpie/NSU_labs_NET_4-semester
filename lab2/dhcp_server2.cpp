#include <iostream>
#include <unordered_map>
#include <string>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <mutex>

#include <set>
#include <map>
#include <algorithm>

const int DHCP_SERVER_PORT = 67;
const std::string SUBNET = "192.168.1.";
// адреса от .100 до .149
const int POOL_START = 100;
const int POOL_SIZE = 50;

// информации о клиенте
struct ClientLease {
    std::string ip;
    time_t lease_time; // время аренды
    std::string offered_ip; // поле для предложенного IP
};

std::map<std::string, ClientLease> lease_table; // хранение аренд IP по MAC адресам
std::set<std::string> used_ips; // Множество занятых IP

std::mutex lease_mutex;
bool running = true;

// генерация IP-адреса на основе MAC
std::string generate_ip(const std::string& mac) {
    // если уже есть аренда - возвращаем существующий IP
    if (lease_table.find(mac) != lease_table.end()) {
        return lease_table[mac].ip;
    }

    // генерация IP из пула - ищу свободный адрес в пуле от pool_start до pool_start+pool_size
    for (int i = 0; i < POOL_SIZE; ++i) {
        std::string ip = SUBNET + std::to_string(POOL_START + i);
        if (used_ips.find(ip) == used_ips.end()) {
            return ip;
        }
    }
    
    throw std::runtime_error("нет свободных IP");
}

// обработка DHCPDISCOVER
void handle_discover(int sock, const std::string& mac, const sockaddr_in& client_addr) {
    std::lock_guard<std::mutex> lock(lease_mutex);

    std::cout << "----------------------------------------" << std::endl;
    std::cout << "Получен DHCPDISCOVER от " << mac << "\n";

    // Генерация нового IP
    std::string ip = generate_ip(mac);

    // Отправка DHCPOFFER
    std::string response = "DHCP_OFFER " + ip;
    sendto(sock, response.c_str(), response.size(), 0,
          (sockaddr*)&client_addr, sizeof(client_addr));

    std::cout << "Отправлен DHCPOFFER: " << ip << " для " << mac << "\n";
    
    // Сохраняем предложенный IP
    lease_table[mac] = {ip, time(nullptr) + 3600, ip}; // Сохраняем предложенный IP
}

// обработка DHCPREQUEST
void handle_request(int sock, const std::string& mac, const std::string& requested_ip, 
                   const sockaddr_in& client_addr) {
    std::lock_guard<std::mutex> lock(lease_mutex);

    std::cout << "Получен DHCPREQUEST от " << mac << " для IP " << requested_ip << "\n";

    // значение по ключу MAC
    auto it = lease_table.find(mac);

    std::cout << "\nЗанятые IP: ";
    for (const auto& ip : used_ips) {
        std::cout << ip << "; ";
    }
    std::cout << "\n" << std::endl;

    // был ли предложен запрашиваемый IP для этого клиента?
    if (it != lease_table.end() && it->second.offered_ip == requested_ip) {
        std::cout << "Запрашиваемый IP совпадает с предложенным: " << requested_ip << std::endl;

        // проверяем, что IP не занят в таблице аренд used_ips
        if (used_ips.find(requested_ip) != used_ips.end()) {
            std::cout << "IP " << requested_ip << " уже занят." << std::endl;
            
            // отправка DHCPNAK клиенту при занятом IP
            std::string response = "DHCP_NAK";
            sendto(sock, response.c_str(), response.size(), 0,
                  (sockaddr*)&client_addr, sizeof(client_addr));
            std::cout << "Отправлен DHCPNAK для " << requested_ip << "\n" << std::endl;
            // удаление аренды it из таблицы
            if (it != lease_table.end()) {
                lease_table.erase(it);
            }
        } else {
            // обновление времени аренды
            it->second.lease_time = time(nullptr) + 3600;

            // Отправка DHCPACK клиенту
            std::string response = "DHCP_ACK " + requested_ip;
            sendto(sock, response.c_str(), response.size(), 0,
                  (sockaddr*)&client_addr, sizeof(client_addr));

            std::cout << "Отправлен DHCPACK для " << requested_ip << "\n" << std::endl;
            used_ips.insert(requested_ip);
        }
    } else {
        std::cout << "Запрашиваемый IP не совпадает с предложенным: " << requested_ip << std::endl;
        std::string response = "DHCP_NAK";
        sendto(sock, response.c_str(), response.size(), 0,
              (sockaddr*)&client_addr, sizeof(client_addr));
        std::cout << "Отправлен DHCPNAK для " << requested_ip << "\n" << std::endl;

        if (it != lease_table.end()) {
            lease_table.erase(it);
        }
    }
}

void lease_cleaner() {
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(30)); // проверка каждые n секунд
        
        std::lock_guard<std::mutex> lock(lease_mutex);
        auto now = time(nullptr);
        
        // Удаляем просроченные аренды
        auto it = lease_table.begin();
        while (it != lease_table.end()) {
            if (it->second.lease_time < now) {
                std::cout << "Очистка: IP " << it->second.ip 
                          << " просрочен для MAC " << it->first << "\n";
                          
                // освобождаем IP
                used_ips.erase(it->second.ip);
                it = lease_table.erase(it);
            } else {
                ++it;
            }
        }
    }
}

int main() {
    // Создание UDP-сокета для DHCP
    int dhcp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (dhcp_sock < 0) {
        std::cerr << "Ошибка создания DHCP-сокета\n";
        return 1;
    }

    // Настройка адреса сервера
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DHCP_SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // привязка сокета к порту DHCP_SERVER_PORT
    if (bind(dhcp_sock, (sockaddr*)&server_addr, sizeof(server_addr))) {
        std::cerr << "Ошибка привязки сокета\n";
        close(dhcp_sock);
        return EXIT_FAILURE;
    }

    std::cout << "DHCP-сервер запущен на порту " << DHCP_SERVER_PORT << "...\n";

    std::thread cleaner(lease_cleaner); // фоновая очистка аренд если дата лизинга истекла
    cleaner.detach();

    char buffer[1024];
    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        
        // получение запроса
        ssize_t received = recvfrom(dhcp_sock, buffer, sizeof(buffer), 0,
                                   (sockaddr*)&client_addr, &client_len);
        
        if (received <= 0) continue;

        buffer[received] = '\0';
        std::string request(buffer);

        // функция нормализации MAC
        auto normalize_mac = [](const std::string& raw_mac) {
            std::string clean_mac = raw_mac;
            clean_mac.erase(
                std::remove(clean_mac.begin(), clean_mac.end(), ' '),
                clean_mac.end()
            );
            return clean_mac;
        };

        // Разбор типа сообщения
        if (request.find("DHCP_DISCOVER ") == 0) {
            std::string raw_mac = request.substr(13);
            std::string mac = normalize_mac(raw_mac);
            handle_discover(dhcp_sock, mac, client_addr);
        }
        else if (request.find("DHCP_REQUEST ") == 0) {
            size_t space_pos = request.find(' ', 13);
            std::string raw_mac = request.substr(13, space_pos - 13);
            std::string mac = normalize_mac(raw_mac);
            std::string ip = request.substr(space_pos + 1);
            handle_request(dhcp_sock, mac, ip, client_addr);
        }
    }

    running = false;
    close(dhcp_sock);
    return 0;
}
