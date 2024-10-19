#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include <fstream>
#include <ctime>
#include <mutex>
#include <sstream>
#include <arpa/inet.h>
#include <unistd.h>
#include <iomanip>
#include <exception>
#include <memory>

namespace {
class SocketException : public std::exception {
 public:
    explicit SocketException(const std::string& message) : message_(message) {}
    const char* what() const noexcept override { return message_.c_str(); }

 private:
    std::string message_;
};
}  // namespace

class Server {
 public:
    explicit Server(std::uint16_t port) : port_(port) {}

    void Start() {
        try {
            auto server_fd = CreateSocket();
            BindSocket(*server_fd);
            ListenOnSocket(*server_fd);

            std::cout << "Server listening on port " << port_ << std::endl;
            AcceptConnections(*server_fd);
        } catch (const SocketException& e) {
            std::cerr << "Server error: " << e.what() << std::endl;
        }
    }

 private:
    std::uint16_t port_;
    std::mutex file_mutex_;

    std::unique_ptr<int, decltype(&close)> CreateSocket() {
        auto server_fd = std::unique_ptr<int, decltype(&close)>(new int(socket(AF_INET, SOCK_STREAM, 0)), &close);
        if (*server_fd == -1) {
            throw SocketException("Failed to create socket");
        }
        return server_fd;
    }

    void BindSocket(int server_fd) {
        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port_);

        if (bind(server_fd, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
            throw SocketException("Failed to bind");
        }
    }

    void ListenOnSocket(int server_fd) {
        if (listen(server_fd, 10) < 0) {
            throw SocketException("Failed to listen");
        }
    }

    void AcceptConnections(int server_fd) {
        while (true) {
            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
            if (client_fd < 0) {
                std::cerr << "Failed to accept connection" << std::endl;
                continue;
            }
            std::thread(&Server::HandleClient, this, client_fd).detach();
        }
    }

    void HandleClient(int client_fd) {
        try {
            char buffer[1024] = {0};
            int bytes_read = read(client_fd, buffer, sizeof(buffer));
            if (bytes_read < 0) {
                throw SocketException("Failed to read from client socket");
            }

            std::lock_guard<std::mutex> lock(file_mutex_);
            std::ofstream log_file("log.txt", std::ios::app);
            if (!log_file.is_open()) {
                throw SocketException("Failed to open log file");
            }
            log_file << buffer << std::endl;
        } catch (const SocketException& e) {
            std::cerr << "Client handling error: " << e.what() << std::endl;
        }
        close(client_fd);
    }
};

class Client {
 public:
    Client(const std::string& name, std::uint16_t port, int period) : name_(name), port_(port), period_(period) {}

    void Start() {
        try {
            while (true) {
                auto sock = CreateSocket();
                ConnectToServer(*sock);
                SendMessage(*sock);
                std::this_thread::sleep_for(std::chrono::seconds(period_));
            }
        } catch (const SocketException& e) {
            std::cerr << "Client error: " << e.what() << std::endl;
        }
    }

 private:
    std::string name_;
    std::uint16_t port_;
    int period_;

    std::unique_ptr<int, decltype(&close)> CreateSocket() {
        auto sock = std::unique_ptr<int, decltype(&close)>(new int(socket(AF_INET, SOCK_STREAM, 0)), &close);
        if (*sock < 0) {
            throw SocketException("Socket creation error");
        }
        return sock;
    }

    void ConnectToServer(int sock) {
        sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port_);

        if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
            throw SocketException("Invalid address/ Address not supported");
        }

        if (connect(sock, reinterpret_cast<struct sockaddr*>(&serv_addr), sizeof(serv_addr)) < 0) {
            throw SocketException("Connection failed");
        }
    }

    void SendMessage(int sock) {
        std::string message = GetCurrentTimestamp() + " " + name_;
        if (send(sock, message.c_str(), message.length(), 0) < 0) {
            throw SocketException("Failed to send message");
        }
    }

    std::string GetCurrentTimestamp() const {
        auto now = std::chrono::system_clock::now();
        auto now_time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::stringstream ss;
        ss << std::put_time(std::localtime(&now_time), "%Y-%m-%d %H:%M:%S") << "." << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }
};

int main(int argc, char* argv[]) {
    try {
        if (argc < 2) {
            throw SocketException("Usage: server <port> or client <name> <port> <period>");
        }

        std::string mode = argv[1];
        if (mode == "server" && argc == 3) {
            std::uint16_t port = static_cast<std::uint16_t>(std::stoi(argv[2]));
            Server server(port);
            server.Start();
        } else if (mode == "client" && argc == 5) {
            std::string name = argv[2];
            std::uint16_t port = static_cast<std::uint16_t>(std::stoi(argv[3]));
            int period = std::stoi(argv[4]);
            Client client(name, port, period);
            client.Start();
        } else {
            throw SocketException("Invalid arguments");
        }
    } catch (const SocketException& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Standard exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
