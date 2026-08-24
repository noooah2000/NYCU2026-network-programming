#include "builtin.hpp"
#include <iostream>
#include <cstdlib>
#include <sys/socket.h>

using namespace std;

void builtin_setenv(const vector<string>& args, UserInfo& user, int user_id) {
    if (args.size() != 3) {
        string err_msg = "Usage: setenv [var] [value]\n";
        send(users[user_id].fd, err_msg.c_str(), err_msg.length(),  MSG_NOSIGNAL);
        return;
    }
    user.env[args[1]] = args[2];
}

void builtin_printenv(const vector<string>& args, UserInfo& user, int user_id) {
    if (args.size() != 2) {
        string err_msg = "Usage: printenv [var]\n";
        send(users[user_id].fd, err_msg.c_str(), err_msg.length(),  MSG_NOSIGNAL);
        return;
    }
    string msg = user.env[args[1]] + "\n";
    send(users[user_id].fd, msg.c_str(), msg.length(),  MSG_NOSIGNAL);
}

void builtin_exit(int user_id) {
    string msg = "*** User '" + users[user_id].nickname + "' left. ***\n";
    for (int i = 1; i <= MAX_USERS; ++i) {
        if (!users[i].is_active || i == user_id) continue;
        send(users[i].fd, msg.c_str(), msg.length(),  MSG_NOSIGNAL);
    }
    users[user_id].reset();
}

void builtin_who(int user_id) {
    string msg = "<ID>\t<fd>\t<nickname>\t<IP:port>\t<indicate me>\n";
    send(users[user_id].fd, msg.c_str(), msg.length(),  MSG_NOSIGNAL);
    for (int i = 1; i <= MAX_USERS; ++i) {
        if (!users[i].is_active) continue;
        msg = to_string(i) + "\t" + to_string(users[i].fd) + "\t" + 
                users[i].nickname + "\t" + users[i].ip+ ":" + to_string(users[i].port);
        msg = (i == user_id) ? (msg + "\t<-me\n") : (msg + "\n");
        send(users[user_id].fd, msg.c_str(), msg.length(),  MSG_NOSIGNAL);
    }
}

void builtin_name(const vector<string>& args, int user_id) {
    if (args.size() != 2) {
        string err_msg = "Usage: name [new name]\n";
        send(users[user_id].fd, err_msg.c_str(), err_msg.length(),  MSG_NOSIGNAL);
        return;
    }
    string new_name = args[1];
    string msg;
    for (int i = 1; i <= MAX_USERS; ++i) {
        if (!users[i].is_active) continue;
        if (new_name == users[i].nickname) {
            msg = "*** User '" + new_name + "' already exists. ***\n";
            send(users[user_id].fd, msg.c_str(), msg.length(),  MSG_NOSIGNAL);
            return;
        }
    }
    users[user_id].nickname = new_name;
    msg = "*** User from " + users[user_id].ip + ":" + to_string(users[user_id].port) + 
            " is named '" + new_name + "'. ***\n";
    for (int i = 1; i <= MAX_USERS; ++i) {
        if (!users[i].is_active) continue;
        send(users[i].fd, msg.c_str(), msg.length(),  MSG_NOSIGNAL);
    }
}

void builtin_yell(const vector<string>& args, int user_id) {
    string yell_msg = (args.size() > 1) ? args[1] : "";
    string msg = "*** " + users[user_id].nickname + " yelled ***: " + yell_msg + "\n";
    for (int i = 1; i <= MAX_USERS; ++i) {
        if (!users[i].is_active) continue;
        send(users[i].fd, msg.c_str(), msg.length(),  MSG_NOSIGNAL);
    }
}

void builtin_tell(const vector<string>& args, int user_id) {
    string err_msg;
    if (args.size() < 3) {
        err_msg = "Usage: tell [user id] [message]\n";
        send(users[user_id].fd, err_msg.c_str(), err_msg.length(), MSG_NOSIGNAL);
        return;
    }

    for (char c : args[1]) {
        if (!isdigit(c)) {
            err_msg = "Usage: tell [user id] [message]\n";
            send(users[user_id].fd, err_msg.c_str(), err_msg.length(), MSG_NOSIGNAL);
            return;
        }
    }

    int target_id = stoi(args[1]);
    if (target_id < 1 || target_id > MAX_USERS || !users[target_id].is_active) {
        err_msg = "*** Error: user #" + args[1] + " does not exist yet. ***\n";
        send(users[user_id].fd, err_msg.c_str(), err_msg.length(),  MSG_NOSIGNAL);
        return;
    }
    string msg ="*** " + users[user_id].nickname + " told you ***: " + args[2] + "\n";
    send(users[target_id].fd, msg.c_str(), msg.length(),  MSG_NOSIGNAL);
}

int builtin_command_handler(const vector<string>& args, int user_id) {
    if (args[0] == "setenv") {
        builtin_setenv(args, users[user_id], user_id);
        return 1;
    }
    else if (args[0] == "printenv") {
        builtin_printenv(args, users[user_id], user_id);
        return 1;
    }
    else if (args[0] == "exit") {
        builtin_exit(user_id);
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