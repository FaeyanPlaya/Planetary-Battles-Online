// MatrixGame - licensed under GPLv2 or any later version.
// Winsock first: windows.h would otherwise bring the old winsock.h.
#include <winsock2.h>
#include <ws2tcpip.h>

#include "NetLink.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace NetLink {
namespace {

SOCKET sock(std::uintptr_t s) { return static_cast<SOCKET>(s); }

void non_blocking(SOCKET s) {
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
    BOOL nodelay = TRUE;  // turns are small and late ones stall every peer
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&nodelay), sizeof(nodelay));
}

bool address_of(const std::string &address, int port, sockaddr_in &out) {
    out = {};
    out.sin_family = AF_INET;
    out.sin_port = htons(static_cast<u_short>(port));
    out.sin_addr.s_addr = inet_addr(address.c_str());  // inet_pton needs a newer Windows target
    return out.sin_addr.s_addr != INADDR_NONE;
}

// Waits until the socket can be read (or written); false on timeout.
bool wait_socket(SOCKET s, bool write, int timeout_ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    return select(0, write ? nullptr : &set, write ? &set : nullptr, nullptr, &tv) > 0;
}

}  // namespace

bool startup() {
    static const bool ok = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
}

Connection::Connection(std::uintptr_t socket) : m_Socket(socket) { non_blocking(sock(m_Socket)); }

Connection::~Connection() { closesocket(sock(m_Socket)); }

void Connection::send_line(const std::string &line) {
    m_Out += line;
    m_Out += '\n';
}

bool Connection::pump() {
    if (!m_Open) return false;
    // Read first: writing to a connection the other side has closed brings back a reset, and a reset
    // throws away what had arrived unread (the host's last line to a person it removed).
    char buffer[16384];
    while (true) {
        const int n = ::recv(sock(m_Socket), buffer, sizeof(buffer), 0);
        if (n > 0) {
            m_In.append(buffer, std::size_t(n));
            continue;
        }
        if (n < 0 && WSAGetLastError() == WSAEWOULDBLOCK) break;
        m_Open = false;  // 0: closed by the other side
        return false;
    }
    while (!m_Out.empty()) {
        const int n = ::send(sock(m_Socket), m_Out.data(), int(m_Out.size()), 0);
        if (n > 0) {
            m_Out.erase(0, std::size_t(n));
            continue;
        }
        if (n < 0 && WSAGetLastError() == WSAEWOULDBLOCK) break;
        m_Open = false;
        return false;
    }
    return m_Open;
}

bool Connection::next_line(std::string &line) {
    const auto end = m_In.find('\n');
    if (end == std::string::npos) return false;
    line.assign(m_In, 0, end);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    m_In.erase(0, end + 1);
    return true;
}

bool Connection::wait_line(std::string &line, int timeout_ms) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        if (next_line(line)) return true;
        if (!pump()) return next_line(line);
        if (next_line(line)) return true;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now());
        if (left.count() <= 0) return false;
        wait_socket(sock(m_Socket), false, int(std::min<long long>(left.count(), 100)));
    }
}

void Connection::flush(int timeout_ms) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (m_Open && !m_Out.empty() && std::chrono::steady_clock::now() < until) {
        pump();
        if (!m_Out.empty()) wait_socket(sock(m_Socket), true, 10);
    }
}

Listener::Listener(const std::string &address, int port) : m_Socket(INVALID_SOCKET) {
    sockaddr_in addr;
    if (!startup() || !address_of(address, port, addr)) return;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return;
    BOOL exclusive = TRUE;  // a second host on the same port fails instead of sharing it
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&exclusive), sizeof(exclusive));
    if (bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 || listen(s, 4) != 0) {
        closesocket(s);
        return;
    }
    m_Socket = s;
}

Listener::~Listener() {
    if (sock(m_Socket) != INVALID_SOCKET) closesocket(sock(m_Socket));
}

bool Listener::listening() const { return sock(m_Socket) != INVALID_SOCKET; }

std::unique_ptr<Connection> Listener::accept(int timeout_ms) {
    if (!listening() || !wait_socket(sock(m_Socket), false, timeout_ms)) return nullptr;
    SOCKET s = ::accept(sock(m_Socket), nullptr, nullptr);
    if (s == INVALID_SOCKET) return nullptr;
    return std::make_unique<Connection>(static_cast<std::uintptr_t>(s));
}

std::unique_ptr<Connection> connect(const std::string &address, int port, int timeout_ms) {
    sockaddr_in addr;
    if (!startup() || !address_of(address, port, addr)) return nullptr;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    do {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return nullptr;
        if (::connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0)
            return std::make_unique<Connection>(static_cast<std::uintptr_t>(s));
        closesocket(s);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    } while (std::chrono::steady_clock::now() < until);
    return nullptr;
}

Connector::Connector(const std::string &address, int port) : m_Socket(INVALID_SOCKET) {
    sockaddr_in addr;
    if (!startup() || !address_of(address, port, addr)) {
        m_Failed = true;
        return;
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        m_Failed = true;
        return;
    }
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
    if (::connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 && WSAGetLastError() != WSAEWOULDBLOCK) {
        closesocket(s);
        m_Failed = true;
        return;
    }
    m_Socket = s;
}

Connector::~Connector() {
    if (sock(m_Socket) != INVALID_SOCKET) closesocket(sock(m_Socket));
}

std::unique_ptr<Connection> Connector::poll() {
    const SOCKET s = sock(m_Socket);
    if (m_Failed || s == INVALID_SOCKET) return nullptr;
    fd_set writable, failed;
    FD_ZERO(&writable);
    FD_ZERO(&failed);
    FD_SET(s, &writable);
    FD_SET(s, &failed);
    timeval now{0, 0};
    const int n = select(0, nullptr, &writable, &failed, &now);
    if (n == 0) return nullptr;
    int error = 0;
    int length = sizeof(error);
    getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &length);
    if (n < 0 || FD_ISSET(s, &failed) || error != 0) {
        closesocket(s);
        m_Socket = INVALID_SOCKET;
        m_Failed = true;
        return nullptr;
    }
    m_Socket = INVALID_SOCKET;
    return std::make_unique<Connection>(static_cast<std::uintptr_t>(s));
}

bool valid_address(const std::string &address) {
    sockaddr_in addr;
    return std::count(address.begin(), address.end(), '.') == 3 && address_of(address, 1, addr);
}

std::vector<std::string> local_addresses() {
    std::vector<std::string> out;
    char name[256] = {};
    if (!startup() || gethostname(name, sizeof(name)) != 0) return out;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo *found = nullptr;
    if (getaddrinfo(name, nullptr, &hints, &found) != 0) return out;
    for (addrinfo *a = found; a; a = a->ai_next) {
        const auto *in = reinterpret_cast<const sockaddr_in *>(a->ai_addr);
        const std::string text = inet_ntoa(in->sin_addr);
        if (!text.starts_with("127.") && std::find(out.begin(), out.end(), text) == out.end()) out.push_back(text);
    }
    freeaddrinfo(found);
    return out;
}

}  // namespace NetLink
