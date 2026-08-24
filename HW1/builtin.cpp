#include "builtin.hpp"
#include <iostream>
#include <cstdlib>

using namespace std;

void builtin_setenv(const vector<string>& args) {
    if (args.size() != 3) {
        cerr << "Usage: setenv [var] [value]" << endl;
        return;
    }
    setenv(args[1].c_str(), args[2].c_str(), 1);   
}

void builtin_printenv(const vector<string>& args) {
    if (args.size() != 2) {
        cerr << "Usage: printenv [var]" << endl;
        return;
    }
    char* val = getenv(args[1].c_str());
    if (val != nullptr) {
        cout << val << endl;
    }
}

bool builtin_command(const vector<string>& args) {
    if (args[0] == "setenv") {
        builtin_setenv(args);
        return true;
    }
    else if (args[0] == "printenv") {
        builtin_printenv(args);
        return true;
    }
    else if (args[0] == "exit") {
        exit(0);
    }
    return false;
}