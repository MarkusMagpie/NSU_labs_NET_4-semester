#include <iostream>
#include <mutex>
#include <thread>
#include <map>
#include <vector>
#include <cstring> // memset
#include <arpa/inet.h> // inet_ntoa
#include <unistd.h> // close

const int ROUTER_PORT = 5000;
const std::string PUBLIC_IP = "203.0.113.1";
const std::vector<std::string> LOCAL_NETWORKS = {"192.168.", "10.", "172.16."}; // RFC 1918

struct ClientInfo {
    std::string ip; // IP адрес клиента
    std::string mac; // MAC адрес клиента
    sockaddr_in address; // сетевой адрес для связи
    time_t last_seen; // время последней активности (пока не используется)
};

std::map<std::string, ClientInfo> clients; // таблица клиентов (MAC -> ClientInfo)
std::map<std::string, std::string> nat_table; // NAT table (внутренний IP -> публичный IP)
std::mutex clients_mutex, nat_mutex;



int createSocket() {
    /*
    socket() создает сокет, возвращает дескриптор сокета
        __domain - AF_INET - протокол IPv4
        __type - SOCK_DGRAM - UDP
        __protocol - 0 - выбирается автоматически
    */
    int sock = socket(AF_INET, SOCK_DGRAM, 0); 

    if (sock < 0) {
        std::cout << "ERROR ошибка создания сокета" << std::endl;
        return 1;
    }

    return sock;
}

bool bindSocket(int sock) {
    // настройка адреса сервера
    sockaddr_in server_addr{}; // стркутура, описывает сокет для работы с протоколами IPv4
    server_addr.sin_family = AF_INET; // IP адрес к которому будет привязан сокет (IPv4)
    server_addr.sin_port = htons(ROUTER_PORT); // номер порта к которому будет привязан сокет
    server_addr.sin_addr.s_addr = INADDR_ANY; // привязка к любому IP-адресу

    // привязка сокета к адресу server_addr
    if (bind(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cout << "ERROR ошибка привязки сокета" << std::endl;
        return false;
    }

    return true;
}

bool isLocalIP(const std::string& ip) {
    if (ip.find("127.") == 0) return true;
    for (const auto& net : LOCAL_NETWORKS) {
        if (ip.find(net) == 0) {
            std::cout << "IP " << ip << " определен как локальный (сеть " << net << ")" << std::endl;
            
            return true;
        }
    }

    std::cout << "IP " << ip << " определен как внешний" << std::endl;
    
    return false;
}

// парсинг MAC адреса из сообщения
std::string parseMAC(const std::string& message) {
    size_t start = message.find("(") + 1;
    size_t end = message.find(")");
    if (start == std::string::npos || end == std::string::npos) {
        throw std::runtime_error("ERROR неверный формат MAC адреса");
    }
    return message.substr(start, end - start);
}

// обработчик сетевых пакетов
/*
    1 - извлечение информации о клиенте (определяю IP из адреса отправителя, MAC из сообщения)  
    2 - обновление таблицы клиентов
    3 - парсинг целевого IP адреса
    4 - (для внешних IP) NAT трансляция (преобразование IP-адреса внутренней сети в публичный)
    5 - поиск целевого клиента
    6 - отправка пакета клиенту
*/
void handlePacket(int sock, sockaddr_in client_addr, char* buffer, ssize_t len) {
    std::cout << "\n[handlePacket]" << std::endl;

    // 1
    // буфер для строкового представления клиентского ip (здесь лежит IP-адрес клиента после inet_ntop)
    char client_ip[INET_ADDRSTRLEN];
    // преобразуем бинарный формат ip-адреса (из client_addr.sin_addr) в читаемую строку
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    
    std::string message(buffer, len);

    std::cout << "Получен пакет от клиента " << client_ip 
                << " размером " << len << " байт" << std::endl;
    std::cout << "Содержимое пакета: " << message << std::endl;

    try {
        // 1
        std::string mac = parseMAC(message);
        std::cout << "Извлечен MAC адрес: " << mac << std::endl;

        // 2 
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            clients[mac] = {client_ip, mac, client_addr, time(nullptr)};
            // std::cout << "Обновлен клиент: MAC: " << mac << "; IP: " << client_ip << std::endl;
            std::cout << "Данные клиента добавлены/обновлены в таблице клиентов по ключу: " << mac <<
                        " - " << client_ip << std::endl;
        }

        // 3 
        // PING [seq] to [target_ip] from [source_ip] ([MAC])
        // PING 0 to 127.0.0.1 from 127.0.0.1 (AA:BB:CC:DD:EE:FF)
        size_t to_pos = message.find("to ");
        size_t from_pos = message.find("from ");
        
        if (to_pos == std::string::npos || from_pos == std::string::npos) {
            std::cout << "ERROR неверный формат пакета" << std::endl;
            return;
        }
        
        // полный целевой адрес (с портом)
        std::string full = message.substr(to_pos + 3, from_pos - to_pos - 4);

        // разделяем IP и порт
        size_t colon = full.find(':');
        std::string target_ip = full.substr(0, colon);
        uint16_t target_port = ROUTER_PORT;

        if (colon != std::string::npos) {
            target_port = static_cast<uint16_t>(std::stoi(full.substr(colon + 1)));
        }

        // проверка обратного ответа - если целевой IP публичный, то заменяем его на внутренний
        if (target_ip == PUBLIC_IP) {
            std::cout << "Целевой IP - публичный, заменяем его на внутренний" << std::endl;
            std::lock_guard<std::mutex> nat_lock(nat_mutex);
            // поиск внутреннего IP по публичному
            for (const auto& [internal_ip, public_ip] : nat_table) {
                size_t pos = message.find(PUBLIC_IP);
                if (public_ip == client_ip) { 
                    // здесь замена публичного IP из исходного пакета на внутренний
                    message.replace(pos, PUBLIC_IP.length(), internal_ip);
                    target_ip = internal_ip;
                    std::cout << "Обратная NAT трансляция: " << PUBLIC_IP << " -> " << internal_ip << std::endl;
                    break;
                }
            }
        }

        std::cout << "Целевой IP: " << target_ip << "; целевый порт: " << target_port << std::endl;

        // 4 
        if (!isLocalIP(target_ip)) {
            std::lock_guard<std::mutex> lock(nat_mutex); // защита NAT-таблицы nat_table от одновременного доступа из разных потоков
            
            // PING 0 to 8.8.8.8 from 192.168.1.2 (AA:BB:CC:DD:EE:FF) -> извлекаю 192.168.1.2
            size_t ip_pos = message.find("from ") + 5;
            size_t ip_end = message.find(" ", ip_pos);
            std::string original_ip = message.substr(ip_pos, ip_end - ip_pos);
            
            // внутренний IP в message с указателя ip_pos -> публичный IP PUBLIC_IP
            message.replace(ip_pos, original_ip.length(), PUBLIC_IP);
            nat_table[original_ip] = PUBLIC_IP; // добавил ключ-значение в таблицу NAT

            // перенаправляем пакет на localhost для теста (вместо реального 8.8.8.8)
            // target_ip = "127.0.0.1"; 
            
            std::cout << "NAT трансляция " << original_ip << " -> " << PUBLIC_IP 
                      << " для внешней цели " << target_ip << std::endl;
            std::cout << "Содержимое пакета после NAT трансляции: " << message << std::endl;
        }

        // 5 
        std::cout << "\nПоиск целевого клиента..." << std::endl;
        ClientInfo target;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (const auto& [_, client] : clients) {
                std::cout << "\t сравнение: " << client.ip << " | " << target_ip << std::endl;
                if (client.ip == target_ip) {
                    target = client;
                    found = true;
                    std::cout << "Целевой клиент найден: " << target.ip << "\n" << std::endl;
                    break;
                }
            }
        }

        // 6 
        if (found) {
            std::cout << "Отправка пакета к целевому клиенту " << target_ip 
                        << " (MAC: " << target.mac << ")" << " размером " << message.size() << " байт" << std::endl;
            
            std::cout << "Содержимое пакета перед отправкой: " << message << std::endl;
            
            sockaddr_in target_addr = target.address;
            target_addr.sin_port = htons(target_port); // устанавливаем нужный порт target_port в адрес получателя
            
            /*
            sendto() отправляет сообщение клиенту через сокет: 
                sock - дескриптор сокета
                message.c_str() - указатель на буфер с данными
                message.size() - размер сообщения
                0 - флаги
                (sockaddr*)&target.address - адрес получателя 
                sizeof(target.address) - его размер соответственно
            */
            sendto(sock, message.c_str(), message.size(), 0, 
                  (sockaddr*)&target_addr, sizeof(target_addr));
        } else {
            std::cout << "ERROR - целевой клиент " << target_ip << " недоступен (не нашел)" << std::endl;
        }

    } catch (const std::exception& e) {
        std::cout << "ERROR - ошибка обработки пакета: " << e.what() << std::endl;
    }

    std::cout << "[handlePacket]" << std::endl;
}

void runRouter(int sock) {
    std::cout << "[runRouter]" << std::endl;
    std::cout << "Маршрутизатор запущен на порту " << ROUTER_PORT << "; публичный IP: " << PUBLIC_IP << std::endl;

    while (true) {
        char buffer[1024];
        sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        /*
        recfvrom() получает сообщение от клиента через сокет:
            sock - дескриптор сокета
            buffer - буфер для данными
            sizeof(buffer) - сколько байт считать из буффера 
            0 - флаги
            (sockaddr*)&clientAddr - адрес получателя
            &addr_len - его размер
        */
        ssize_t recvlen = recvfrom(sock, buffer, sizeof(buffer), 0,
                              (sockaddr*)&client_addr, &addr_len);
        if (recvlen < 0) {
            std::cout << "ERROR ошибка получения данных" << std::endl;
            continue;
        }
        
        // обработчика полученных пакетов запускаем в отдельном потоке
        std::thread(handlePacket, sock, client_addr, buffer, recvlen).detach();
    }
}

int main() {
    int sock = createSocket();

    if (!bindSocket(sock)) {
        close(sock);
        return 0;
    }

    runRouter(sock);

    close(sock);
    return 0;
}