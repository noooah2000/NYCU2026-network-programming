#include "builtin.hpp"
#include "types.hpp"
#include <iostream>
#include <cstdlib>
#include <signal.h>
#include <cstring>

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

void builtin_who(int user_id) {
    string msg = "<ID>\t<fd>\t<nickname>\t<IP:port>\t<indicate me>";
    cout << msg << endl;
    for (int i = 1; i <= MAX_USERS; ++i) {
        if (!shm->users[i].is_active) continue;
        msg = to_string(i) + "\t" + to_string(shm->users[i].fd) + "\t" + 
              string(shm->users[i].nickname) + "\t" + shm->users[i].ip+ ":" + to_string(shm->users[i].port);
        if (i == user_id) msg += "\t<-me";
        cout << msg << endl;
    }
}

void builtin_name(const vector<string>& args, int user_id) {
    if (args.size() != 2) {
        string err_msg = "Usage: name [new name]";
        cout << err_msg << endl;
        return;
    }
    string new_name = args[1];
    for (int i = 1; i <= MAX_USERS; ++i) {
        if (shm->users[i].is_active && new_name == shm->users[i].nickname) {
            cout << "*** User '" + new_name + "' already exists. ***" << endl;
            return;
        }
    }
    strcpy(shm->users[user_id].nickname, new_name.c_str());
    string msg = "*** User from " + string(shm->users[user_id].ip) + ":" + to_string(shm->users[user_id].port) + 
                 " is named '" + new_name + "'. ***";
    broadcast(msg + "\n");
}

void builtin_yell(const vector<string>& args, int user_id) {
    string yell_msg = (args.size() > 1) ? args[1] : "";
    string msg = "*** " + string(shm->users[user_id].nickname) + " yelled ***: " + yell_msg;
    broadcast(msg + "\n");
}

void builtin_tell(const vector<string>& args, int user_id) {
    if (args.size() < 3) {
        cout << "Usage: tell [user id] [message]" << endl;
        return;
    }

    for (char c : args[1]) {
        if (!isdigit(c)) {
            cout << "Usage: tell [user id] [message]" << endl;
            return;
        }
    }

    int target_id = stoi(args[1]);
    if (target_id < 1 || target_id > MAX_USERS || !shm->users[target_id].is_active) {
        cout << "*** Error: user #" + args[1] + " does not exist yet. ***" << endl;
        return;
    }
    string msg ="*** " + string(shm->users[user_id].nickname) + " told you ***: " + args[2];
    strcpy(shm->broadcast_msg, (msg + "\n").c_str());
    kill(shm->users[target_id].pid, SIGUSR1);
}

int builtin_command_handler(const vector<string>& args, int user_id) {
    if (args[0] == "setenv") {
        builtin_setenv(args);
        return 1;
    }
    else if (args[0] == "printenv") {
        builtin_printenv(args);
        return 1;
    }
    else if (args[0] == "exit") {
        return -1;
    }
    else if (args[0] == "who") {
        builtin_who(user_id);
        return 1;
    }
    else if (args[0] == "tell") {
        builtin_tell(args, user_id);
        return 1;
    }
    else if (args[0] == "yell") {
        builtin_yell(args, user_id);
        return 1;
    }
    else if (args[0] == "name") {
        builtin_name(args, user_id);
        return 1;
    }
    
    return 0;
}