#include <iostream>
#include <fstream>
#include "nlohmann/json.hpp"
 
using json = nlohmann::json;
 
int main()
{
    json j;
    j["123"] = 123;
    std::cout << j << std::endl;
    return 0;
}