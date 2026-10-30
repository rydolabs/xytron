#include <future>
#include <iostream>
#include <map>
#include <string>
#include <vector>

int main() {
    std::vector<int> values{1, 2, 3, 4};
    std::map<std::string, int> lookup{{"answer", 42}};

    std::future<int> result = std::async(std::launch::async, [values, lookup]() {
        int sum = 0;
        for (std::size_t i = 0; i < values.size(); ++i) {
            sum += values[i];
        }
        return sum + lookup.at("answer");
    });

    const int value = result.get();
    if (value != 52) {
        std::cerr << "unexpected result: " << value << std::endl;
        return 1;
    }

#if defined(_WIN32)
    std::cout << "platform=windows" << std::endl;
#elif defined(__linux__)
    std::cout << "platform=linux" << std::endl;
#else
    std::cout << "platform=other" << std::endl;
#endif

    std::cout << "cxx=" << __cplusplus << std::endl;
    std::cout << "smoke=ok" << std::endl;
    return 0;
}
