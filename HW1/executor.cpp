#include "executor.hpp"
#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

using namespace std;

void external_command(vector<string> args) {
    vector<char*> c_args;
    for (const auto& s : args) {
        c_args.push_back(const_cast<char*>(s.c_str()));
    }
    c_args.push_back(nullptr);
    if (execvp(c_args[0], c_args.data()) == -1) {
        cerr << "Unknown command: [" << args[0] << "]." << endl;
        exit(0);
    }
}

void redirection(CommandFD& curr_fd, string& redirect_file) {
    curr_fd.out = open(redirect_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (curr_fd.out < 0) {
        perror("open error");
        exit(1);
    }
}

void ordinary_pipe(CommandFD& curr_fd, CommandFD& next_fd) {
    int fd[2];
    pipe(fd);
    curr_fd.out = fd[1];
    next_fd.in = fd[0];
}

void numbered_pipe(CommandFD& curr_fd, int pipe_target) {
    int N = (curr_line + pipe_target) % MAX_N;
    NumPipeState& target_NPS = pipe_map[N];
    if (!target_NPS.in_use) {
        int fd[2];
        pipe(fd);
        target_NPS.write_fd = fd[1];
        target_NPS.read_fd = fd[0];
        target_NPS.in_use = true;
    }
    curr_fd.out = target_NPS.write_fd;
}

void execute_child(Command& cmd, int pipe_target, CommandFD& curr_fd, CommandFD& next_fd) {
    if (curr_fd.in != STDIN_FILENO) {
        dup2(curr_fd.in, STDIN_FILENO);
        close(curr_fd.in);
    }
    if (curr_fd.out != STDOUT_FILENO) {
        dup2(curr_fd.out, STDOUT_FILENO);
        if (cmd.type == ERR_NPIPE) {
            dup2(curr_fd.out, STDERR_FILENO);
        }
        close(curr_fd.out);
    }
    if (next_fd.in != STDIN_FILENO) close(next_fd.in);
    if (cmd.type >= NPIPE) {
        int N = (curr_line + pipe_target) % MAX_N;
        NumPipeState& target_NPS = pipe_map[N];
        close(target_NPS.read_fd);
    }
    external_command(cmd.args);
}

void execute_parent(Command& cmd, CommandFD& curr_fd, vector<pid_t>& children, pid_t& child_pid) {
    if (curr_fd.in != STDIN_FILENO) close(curr_fd.in);
    if (curr_fd.out != STDOUT_FILENO && (cmd.type < NPIPE)) close(curr_fd.out);
    if (cmd.type < NPIPE) children.push_back(child_pid);
}

void execute_pipeline(CommandGroup& group) {
    CommandFD curr_fd;
    vector<Command>& cmds = group.cmds;
    NumPipeState& curr_NPS = pipe_map[curr_line % MAX_N];
    if (curr_NPS.in_use == true) {
        curr_fd.in = curr_NPS.read_fd;
        close(curr_NPS.write_fd);
    }

    vector<pid_t> children;
    for (size_t i = 0; i < cmds.size(); ++i) {
        CommandFD next_fd;
        
        if (i != (cmds.size() - 1)) {
            ordinary_pipe(curr_fd, next_fd);
        }
        else if (cmds[i].type == REDIR) {
            redirection(curr_fd, group.redirect_file);
        }
        else if (cmds[i].type == NPIPE || cmds[i].type == ERR_NPIPE) {
            numbered_pipe(curr_fd, group.pipe_target);
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
            execute_child(cmds[i], group.pipe_target, curr_fd, next_fd);
        }
        else { // pid > 0
            execute_parent(cmds[i], curr_fd, children, pid);
        }
        curr_fd = next_fd;
    }
    if (curr_NPS.in_use == true) {
        curr_NPS.in_use = false;
    }
    if (group.pipe_target != 0) return; // prevent deadlock which cause by big file
    for (pid_t child : children) {
        waitpid(child, nullptr, 0);
    }
}