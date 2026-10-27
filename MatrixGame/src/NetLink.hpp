// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Line-based TCP connections for battles shared over the network (Winsock, no extra libraries).
// Every message is one line of text; sending and receiving never block once connected.
namespace NetLink {

bool startup();  // WSAStartup; false when Winsock is unavailable

class Connection {
public:
    Connection(std::uintptr_t socket);
    ~Connection();
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;

    void send_line(const std::string &line);
    // Sends what is queued and reads what has arrived; false once the connection is gone.
    bool pump();
    // The next complete received line (without the line end), if any.
    bool next_line(std::string &line);
    // Waits up to timeout_ms for a complete line; false on timeout or a closed connection.
    bool wait_line(std::string &line, int timeout_ms);
    // Sends everything queued, waiting up to timeout_ms (before closing).
    void flush(int timeout_ms);
    bool open() const { return m_Open; }

private:
    std::uintptr_t m_Socket;
    bool m_Open = true;
    std::string m_In, m_Out;
};

class Listener {
public:
    // Listens on address:port; address 127.0.0.1 keeps it to this PC.
    Listener(const std::string &address, int port);
    ~Listener();
    bool listening() const;
    // Waits up to timeout_ms for a connection.
    std::unique_ptr<Connection> accept(int timeout_ms);

private:
    std::uintptr_t m_Socket;
};

// Connects to address:port, retrying until timeout_ms passes (the host may still be starting).
std::unique_ptr<Connection> connect(const std::string &address, int port, int timeout_ms);

// One connection attempt that never blocks: poll it every frame until it connects or fails.
class Connector {
public:
    Connector(const std::string &address, int port);
    ~Connector();
    Connector(const Connector &) = delete;
    Connector &operator=(const Connector &) = delete;
    // The connection once it is made (once), else nullptr.
    std::unique_ptr<Connection> poll();
    bool failed() const { return m_Failed; }

private:
    std::uintptr_t m_Socket;
    bool m_Failed = false;
};

// Whether the text is an IPv4 address that connect and Listener take.
bool valid_address(const std::string &address);

// IPv4 addresses of this PC that other PCs may reach (not 127.x), for showing to people who join.
std::vector<std::string> local_addresses();

}  // namespace NetLink
