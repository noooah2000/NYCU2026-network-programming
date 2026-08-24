#include <iostream>
#include <string>
#include <map>
#include <sys/select.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <algorithm>
#include <sys/wait.h>
#include <cstring>
#include "types.hpp"
#include "parser.hpp"
#include "executor.hpp"
#include "builtin.hpp"

using namespace std;
UserInfo users[MAX_USERS + 1];
UserPipeTable user_pipe_table;

// void sigchld_handler(int) {
//     while (waitpid(-1, nullptr, WNOHANG) > 0);
// }

int setup_socket(uint16_t port) {
    int sockfd;
    sockaddr_in serv_addr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("server: can't open stream socket");
        exit(1);
    }
    int optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    if (::bind(sockfd, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("server: can't bind local address");
        exit(1);
    }
    listen(sockfd, SOMAXCONN);
    return sockfd;
}

void handle_new_connection(int listen_fd, fd_set& active_fds, int& max_fd) {
    sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);
    int conn_fd = accept(listen_fd, (sockaddr*)&cli_addr, &clilen);
    if (conn_fd < 0) {
        perror("server: accept error");
        return;
    }

    int new_id = -1;
    for (int i = 1; i <= MAX_USERS; ++i) {
        if (!users[i].is_active) {
            users[i].reset();
            users[i].is_active = true;
            users[i].fd = conn_fd;
            users[i].ip = inet_ntoa(cli_addr.sin_addr);
            users[i].port = ntohs(cli_addr.sin_port);
            new_id = i;
            break;
        }
    }
    if(new_id == -1){
        close(conn_fd);
        cerr << "Too many users" << endl;
        return;
    }
    string welcome_msg = "****************************************\n"
                         "** Welcome to the information server. **\n"
                         "****************************************\n";
    send(conn_fd, welcome_msg.c_str(), welcome_msg.length(), MSG_NOSIGNAL);

    string login_msg = "*** User '" + users[new_id].nickname + "' entered from " + 
                                users[new_id].ip + ":" + to_string(users[new_id].port) + ". ***\n";

    for (int i = 1; i <= MAX_USERS; ++i) {
        if (users[i].is_active) 
            send(users[i].fd, login_msg.c_str(), login_msg.length(), MSG_NOSIGNAL);
        
    }
                    
    string prompt = "% ";
    send(conn_fd, prompt.c_str(), prompt.length(), MSG_NOSIGNAL);

    FD_SET(conn_fd, &active_fds);
    max_fd = max(max_fd, conn_fd);
}

void handle_client_disconnect(int user_id, int client_fd, fd_set& active_fds) {
    user_pipe_table.clear(user_id);
    FD_CLR(client_fd, &active_fds);
    close(client_fd);
}

bool handle_input(int user_id, const string& input, fd_set& active_fds) {
    vector<CommandGroup> cmd_groups;
    parser(input, cmd_groups);

    for (CommandGroup& group : cmd_groups) {
        if (group.cmds.empty()) continue;
        string err_msg = validate_single_group(group);
        if (!err_msg.empty()) {
            send(users[user_id].fd, err_msg.c_str(), err_msg.length(), MSG_NOSIGNAL);
            continue;
        }
        users[user_id].curr_line += 1; 

        int client_fd = users[user_id].fd;
        int builtin_status = builtin_command_handler(group.cmds[0].args, user_id);
        if (builtin_status < 0) {
            handle_client_disconnect(user_id, client_fd, active_fds);
            return false;
        }
        else if (builtin_status > 0) {
            NumPipeState& curr_NPS = users[user_id].pipe_map[users[user_id].curr_line % MAX_N];
            if (curr_NPS.in_use == true) {
                close(curr_NPS.read_fd);
                close(curr_NPS.write_fd);
                curr_NPS.in_use = false;
            }
            continue;
        } 
        setenv("PATH", users[user_id].env["PATH"].c_str(), 1);
        execute_pipeline(group, user_id, input);
    }   
    return true;
}

void process_client_inputs(int user_id, fd_set& active_fds) {
    char buffer[15000];
    memset(buffer, 0, sizeof(buffer));
    int bytes_read = read(users[user_id].fd, buffer, sizeof(buffer) - 1);

    if (bytes_read <= 0) {
        int client_fd = users[user_id].fd;
        builtin_command_handler({"exit"}, user_id);
        handle_client_disconnect(user_id, client_fd, active_fds);
        return;
    }
    users[user_id].cmd_buffer += buffer;

    size_t pos;
    while ((pos = users[user_id].cmd_buffer.find('\n')) != string::npos) {
        string input = users[user_id].cmd_buffer.substr(0, pos);
        users[user_id].cmd_buffer.erase(0, pos + 1);

        if (!input.empty() && input.back() == '\r') input.pop_back();
        
        if (!input.empty()) {
            if (!handle_input(user_id, input, active_fds)) return; 
        }
        string prompt = "% ";
        send(users[user_id].fd, prompt.c_str(), prompt.length(), MSG_NOSIGNAL);
    }
}

int main(int argc, char *argv[]) {
    uint16_t port = (argc == 2) ? atoi(argv[1]) : 7777;
    int listen_fd = setup_socket(port);
    int max_fd = listen_fd;

    fd_set active_fds, read_fds;
    FD_ZERO(&active_fds);
    FD_SET(listen_fd, &active_fds);

    // signal(SIGCHLD, sigchld_handler);
    while (true) {  
        while (waitpid(-1, nullptr, WNOHANG) > 0);

        read_fds = active_fds;
        if (select(max_fd + 1, &read_fds, nullptr, nullptr, nullptr) < 0) {
            if (errno == EINTR) continue;
            perror("server: select error");
            exit(1);
        }

        if (FD_ISSET(listen_fd, &read_fds)) {
            handle_new_connection(listen_fd, active_fds, max_fd);
        }

        for (int fd = 0; fd <= max_fd; ++fd) {
            if (fd == listen_fd || !FD_ISSET(fd, &read_fds)) continue;

            int user_id = -1;
            for (int i = 1; i <= MAX_USERS; ++i) {
                if (users[i].is_active && users[i].fd == fd) {
                    user_id = i;
                    break;
                }
            }
            if (user_id == -1) continue;
            process_client_inputs(user_id, active_fds);
        }
    }
    return 0;
}