#include <iostream>
#include <vector>
#include <map>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

// структура DHCP-пакета: https://support.huawei.com/enterprise/en/doc/EDOC1100174721/2b689419/dhcp
struct DHCPPacket {
    uint8_t op;         // тип сообщения: 1 = запрос клиента, 2 = ответ сервера
    uint8_t htype;      // тип аппаратного адреса 1 = Ethernet
    uint8_t hlen;       // длина аппаратного адреса (6 для MAC)
    uint32_t xid;       // идентификатор транзакции
    uint16_t flags;     // флаги
    uint32_t yiaddr;    // IPv4 address that a server assigns to a client.
    uint32_t siaddr;    // IPv4 address of the server to be used in the next phase of the DHCP process.
    uint32_t giaddr;    // IP ретранслятора
    uint8_t chaddr[16]; // MAC-адрес клиента
};

class DHCPServer {
private:
    int sock; // UDP-сокет для приема/отправки пакетов
    struct sockaddr_in serverAddr; // адрес сервера (слушает: все интерфейсы, порт 67)
    std::map<std::string, uint32_t> leases; // тпблица аренды IP адресов
    uint32_t currentIP; // текущий IP для выдачи клиентам

public:
    DHCPServer(uint32_t startIP) : currentIP(startIP) {
        // создание UDP-сокета
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(67); // сервер слушает порт 67
        serverAddr.sin_addr.s_addr = INADDR_ANY; // сервер слушает все сетевые интерфейсы

        // привязка сокета к адресу (Give the socket FD the local address ADDR)
        // https://pubs.opengroup.org/onlinepubs/009619199/bind.htm
        bind(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    }

    // обработка DHCP Discover
    void handleDiscover(const DHCPPacket& packet) {
        // генерация нового IP
        std::string mac(reinterpret_cast<const char*>(packet.chaddr), 6);
        if (leases.find(mac) == leases.end()) {
            leases[mac] = currentIP++; // если MAC-адрес новый, то выделяется IP из пула (currentIP++)
        }
        
        // формирование DHCP Offer
        DHCPPacket offer = packet;
        offer.op = 2;
        offer.yiaddr = leases[mac];
        offer.siaddr = inet_addr("192.168.1.1"); // IP сервера
        
        struct sockaddr_in clientAddr;
        clientAddr.sin_family = AF_INET;
        clientAddr.sin_port = htons(68); // клиентский порт
        clientAddr.sin_addr.s_addr = INADDR_BROADCAST; // широковещательный адрес
        
        // отправка DHCP Offer
        sendto(sock, &offer, sizeof(offer), 0, 
              (struct sockaddr*)&clientAddr, sizeof(clientAddr));
        
        std::cout << "Отправлен DHCP Offer для MAC-адреса: " << mac << " с IP-адресом: " << inet_ntoa({offer.yiaddr}) << std::endl;
    }

    void run() {
        while(true) {
            DHCPPacket packet;
            struct sockaddr_in clientAddr;
            socklen_t len = sizeof(clientAddr);
            
            // прием пакета
            ssize_t info = recvfrom(sock, &packet, sizeof(packet), 0, (struct sockaddr*)&clientAddr, &len);
            if (info < 0) {
                std::cerr << "ошибка при приеме пакета" << std::endl;
                continue;
            }
            
            if (packet.op == 1) { // если тип сообщения - запрос клиента, то считаю что это DHCP Discover
                std::cout << "запустил дисковер" << std::endl;
                handleDiscover(packet);
            }
        }
    }
};

int main() {
    DHCPServer server(inet_addr("192.168.1.100")); // начальный IP пула
    server.run();
    return 0;
}

// g++ dhcp_server.cpp -o dhcp_server -lpthread
// sudo ./dhcp_server