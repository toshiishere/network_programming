#pragma once
#include <string>

bool send_message(int sock, const std::string &msg);
std::string recv_message(int sock);
