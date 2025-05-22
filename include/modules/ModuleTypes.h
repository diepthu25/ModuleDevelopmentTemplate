// This file used to define the data structure used accross the modules.
#include <iostream>

struct SimpleResource {
    int a;
    int b;
    void input() {
        std::cout << "Enter two integers (a and b): ";
        std::cin >> a >> b;
    }
};
