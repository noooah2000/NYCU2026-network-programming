#include <iostream>
#include <string>
#include <map>
#include <sys/select.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <algorithm>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>
#include <cstring>
#include <dirent.h>
#include "types.hpp"
#include "parser.hpp"
#include "executor.hpp"
#include "builtin.hpp"
#include "run_npshell.hpp"

using namespace std;

SharedMemory* shm = nullptr;

void sigchld_handler(int) {
    while (waitpid(-1, nullptr, WNOHANG) > 0);
}
void broadcast_handler(int) {
    write(STDOUT_FILENO, shm->broadcast_msg, strlen(shm->broadcast_msg));
}

void clear_user_pipe_dir() {
    DIR *dir = opendir("user_pipe");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != nullptr) {
            if (string(ent->d_name) != "." && string(ent->d_name) != "..") {
                string filepath = "user_pipe/" + string(ent->d_name);
                unlink(filepath.c_str()); // 使用正規的 POSIX 系統呼叫刪除檔案
            }
        }
        closedir(dir);
    }
}

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

void broadcast(const string& msg) {
    strcpy(shm->broadcast_msg, msg.c_str());
    for (int i = 1; i <= MAX_USERS; ++i) {
        if (shm->users[i].is_active) kill(shm->users[i].pid, SIGUSR1);
    }
}

void handle_client_disconnect(int user_id) {
    string nickname(shm->users[user_id].nickname);
    
    shm->users[user_id].is_active = false;
    shm->users[user_id].fd = -1;
    strcpy(shm->users[user_id].nickname, "(no name)");
    memset(shm->users[user_id].ip, 0, sizeof(shm->users[user_id].ip));
    shm->users[user_id].port = 0;
    shm->users[user_id].pid = 0;

    for (int i = 1; i <= MAX_USERS; ++i) {
        string fifo_in = "user_pipe/" + to_string(i) + "_" + to_string(user_id);
        string fifo_out = "user_pipe/" + to_string(user_id) + "_" + to_string(i);

        unlink(fifo_in.c_str());
        unlink(fifo_out.c_str());
    }
    broadcast("*** User '" + nickname + "' left. ***\n");
}

void handle_client(int user_id, int conn_fd) {
    struct sigaction sa;
    sa.sa_handler = broadcast_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &sa, nullptr);

    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_DFL);

    dup2(conn_fd, STDIN_FILENO);
    dup2(conn_fd, STDOUT_FILENO);
    dup2(conn_fd, STDERR_FILENO);
    string welcome_msg = "****************************************\n"
                         "** Welcome to the information server. **\n"
                         "****************************************\n";
    cout << welcome_msg << flush;

    string login_msg = "*** User '" + string(shm->users[user_id].nickname) + "' entered from " + 
                       string(shm->users[user_id].ip) + ":" + to_string(shm->users[user_id].port) + ". ***";
    broadcast(login_msg + "\n");

    run_npshell(user_id);

    handle_client_disconnect(user_id);
}

int main(int argc, char *argv[]) {
    uint16_t port = (argc == 2) ? atoi(argv[1]) : 7777;
    shm = (SharedMemory*)mmap(nullptr, sizeof(SharedMemory), 
                              PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    new (shm) SharedMemory();
    clear_user_pipe_dir();

    int listen_fd = setup_socket(port);
    signal(SIGCHLD, sigchld_handler);

    while (true) {
        sockaddr_in cli_addr;
        socklen_t clilen = sizeof(cli_addr);
        int conn_fd = accept(listen_fd, (sockaddr*)&cli_addr, &clilen);
        if (conn_fd < 0) {
            perror("server: accept error");
            continue;
        }

        int new_id = -1;
        for (int i = 1; i <= MAX_USERS; ++i) {
            if (!shm->users[i].is_active) {
                new_id = i;
                shm->users[new_id].is_active = true;
                shm->users[new_id].fd = conn_fd;
                strcpy(shm->users[new_id].nickname, "(no name)");
                strcpy(shm->users[new_id].ip, inet_ntoa(cli_addr.sin_addr));
                shm->users[new_id].port = ntohs(cli_addr.sin_port);
                break;
            }
        }

        if (new_id == -1) {
            close(conn_fd);
            cerr << "Too many users" << endl;
            continue;
        }

        pid_t pid;
        while ((pid = fork()) < 0) {
            if (errno == EAGAIN) {
                waitpid(-1, nullptr, 0);
                while (waitpid(-1, nullptr, WNOHANG) > 0); // Non-blocking
            }
            else {
                perror("fork error");
                exit(1);
            }
        }
        if (pid == 0) {
            close(listen_fd);
            shm->users[new_id].pid = getpid();
            handle_client(new_id, conn_fd);
            exit(0);
        } 
        else if (pid > 0) {
            close(conn_fd);
        }
    }
}