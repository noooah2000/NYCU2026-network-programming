#include <iostream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "run_npshell.hpp"

using namespace std;

void sigchld_handler(int) {
    while (waitpid(-1, nullptr, WNOHANG) > 0);
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

void handle_client(int sockfd, int newsockfd) {
    close(sockfd);
    signal(SIGCHLD, SIG_DFL);

    dup2(newsockfd, STDIN_FILENO);
    dup2(newsockfd, STDOUT_FILENO);
    dup2(newsockfd, STDERR_FILENO);
    close(newsockfd);

    run_npshell();
    exit(0);
}

int main(int argc, char* argv[]) {
    uint16_t port = 7777;
    if (argc == 2) port = atoi(argv[1]);

    int sockfd = setup_socket(port);
    signal(SIGCHLD, sigchld_handler);

    cout << "Server is listening on port " << port << "..." << endl;
    for (;;) {
        sockaddr_in cli_addr;
        socklen_t clilen = sizeof(cli_addr);
        int newsockfd = accept(sockfd, (sockaddr*)&cli_addr, &clilen);

        if (newsockfd < 0) {
            perror("server: accept error");
            continue;
        }
        cout << "New connection accepted!" << endl;

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
            handle_client(sockfd, newsockfd);
        }
        else { // pid > 0
            close(newsockfd);
        }
    }
    return 0;
}