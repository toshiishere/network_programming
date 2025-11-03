#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <ctime>
#include <sstream>
#include <iomanip>

bool send_message(int sock, const std::string &msg) {
    unsigned len = msg.size();
    uint32_t net_len = htonl(len);  // convert to network byte order

    if (write(sock, &net_len, sizeof(net_len)) != sizeof(net_len)) {
        return false;
    }
    ssize_t sent = 0;
    while (sent < (ssize_t)msg.size()) {
        ssize_t n = write(sock, msg.data() + sent, msg.size() - sent);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

std::string recv_message(int sock) {
    unsigned net_len;
    unsigned n = read(sock, &net_len, sizeof(net_len));
    if (n == 0) return "Disconnected"; // connection closed
    if (n != sizeof(net_len)) return std::string(); // incomplete header or error
    if (n>65536) return std::string(); //over max size

    unsigned len = ntohl(net_len);
    std::vector<char> buffer(len);

    size_t received = 0;
    while (received < len) {
        ssize_t r = read(sock, buffer.data() + received, len - received);
        if (r <= 0) return std::string();
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