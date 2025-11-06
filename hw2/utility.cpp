#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <cerrno>

bool send_message(int sock, const std::string &msg) {
    unsigned len = msg.size();
    uint32_t net_len = htonl(len);
    size_t header_sent = 0;
    while (header_sent < sizeof(net_len)) {
        ssize_t n = write(sock, ((char*)&net_len) + header_sent, sizeof(net_len) - header_sent);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Would block, retry
                continue;
            }
            // Actual error
            return false;
        }
        if (n == 0) return false; // connection closed
        header_sent += n;
    }

    // Send the message body
    ssize_t sent = 0;
    while (sent < (ssize_t)msg.size()) {
        ssize_t n = write(sock, msg.data() + sent, msg.size() - sent);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Would block, retry
                continue;
            }
            // Actual error
            return false;
        }
        if (n == 0) return false; // connection closed
        sent += n;
    }
    return true;
}

std::string recv_message(int sock) {
    uint32_t net_len;
    size_t header_received = 0;

    // Read the 4-byte length header
    while (header_received < sizeof(net_len)) {
        ssize_t n = read(sock, ((char*)&net_len) + header_received, sizeof(net_len) - header_received);
        if (n == 0) return "Disconnected"; // connection closed
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No data available right now (non-blocking)
                return std::string();
            }
            // Actual error
            return std::string();
        }
        header_received += n;
    }

    unsigned len = ntohl(net_len);

    // Check if length is valid
    if (len == 0 || len > 65536) {
        return std::string(); // invalid length
    }

    std::vector<char> buffer(len);
    size_t received = 0;

    while (received < len) {
        ssize_t r = read(sock, buffer.data() + received, len - received);
        if (r == 0) return std::string(); // connection closed mid-message
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Would block - continue waiting
                continue;
            }
            // Actual error
            return std::string();
        }
        received += r;
    }

    return std::string(buffer.begin(), buffer.end());
}
std::string now_time_str() {
    std::time_t t = std::time(nullptr);
    std::tm tm;
    localtime_r(&t, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}