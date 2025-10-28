#include <iostream>
#include "nlohmann/json.hpp"
 
using json = nlohmann::json;
using namespace std;

int main()
{
    json j;
    j["123"] = 123;
    std::cout << j << std::endl;
    return 0;
}