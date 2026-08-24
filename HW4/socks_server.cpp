#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <utility>
#include <unistd.h>
#include <csignal>
#include <fstream>
#include <sstream>
#include <regex>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;

constexpr int SOCKS4 = 1;
constexpr int SOCKS4A = 2;
constexpr uint8_t SOCKS_CONNECT = 1;
constexpr uint8_t SOCKS_BIND = 2;
constexpr uint8_t SOCKS_ACCEPT = 90;
constexpr uint8_t SOCKS_REJECT = 91;
struct SOCKSINFO {
    int type = SOCKS4;
    uint8_t vn;
    uint8_t cd;
    uint16_t dst_port;
    std::string dst_ip;
    std::string userid;
    std::string domain_name;

    void print_info(const tcp::endpoint& client_ep, const std::string& reply_status) {
        std::string command = (cd == SOCKS_CONNECT) ? "CONNECT" : 
                                (cd == SOCKS_BIND) ? "BIND" : "UNKNOWN";
        std::cout << "========== New Connect ==========" << std::endl;
        std::cout << "<S_IP>: " << client_ep.address().to_string() << std::endl;
        std::cout << "<S_PORT>: " << client_ep.port() << std::endl;
        std::cout << "<D_IP>: " << dst_ip << std::endl;
        std::cout << "<D_PORT>: " << dst_port << std::endl;
        std::cout << "<Command>: " << command << std::endl;
        std::cout << "<Reply>: " << reply_status << std::endl;
    }
};
class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket accept_socket) : 
        client_socket_(std::move(accept_socket)),
        target_socket_(client_socket_.get_executor()),
        resolver_(client_socket_.get_executor()),
        bind_acceptor_(client_socket_.get_executor()) {}

    void start() {
        do_read_header();
    }
private:
    SOCKSINFO socks_info_;
    tcp::socket client_socket_;
    tcp::socket target_socket_;
    tcp::resolver resolver_;
    tcp::acceptor bind_acceptor_;

    static constexpr int max_length = 4096;
    uint8_t socks_header_buf_[8];
    std::string dynamic_buf_;
    uint8_t socks_reply_buf_[8];
    uint8_t c2t_buf_[max_length];
    uint8_t t2c_buf_[max_length];

    void do_read_header() {
        auto self(shared_from_this());
        boost::asio::async_read(client_socket_, boost::asio::buffer(socks_header_buf_, 8),
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec) {
                socks_info_.vn = socks_header_buf_[0];
                socks_info_.cd = socks_header_buf_[1];
                socks_info_.dst_port = (socks_header_buf_[2] << 8) | socks_header_buf_[3];
                socks_info_.dst_ip = std::to_string(socks_header_buf_[4]) + "." +
                                     std::to_string(socks_header_buf_[5]) + "." +
                                     std::to_string(socks_header_buf_[6]) + "." +
                                     std::to_string(socks_header_buf_[7]);

                socks_info_.type = (socks_header_buf_[4] == 0 && socks_header_buf_[5] == 0 && 
                                    socks_header_buf_[6] == 0 && socks_header_buf_[7] != 0) ? SOCKS4A : SOCKS4;
                do_read_userid();
            }
        });
    }

    void do_read_userid() {
        auto self(shared_from_this());
        boost::asio::async_read_until(client_socket_, boost::asio::dynamic_buffer(dynamic_buf_), '\0',
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec) {
                socks_info_.userid = dynamic_buf_.substr(0, length - 1);
                dynamic_buf_.erase(0, length);
                if (socks_info_.type == SOCKS4A) do_read_domain_name();
                else process_request();
            }
        });
    }

    void do_read_domain_name() {
        auto self(shared_from_this());
        boost::asio::async_read_until(client_socket_, boost::asio::dynamic_buffer(dynamic_buf_), '\0',
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec) {
                socks_info_.domain_name = dynamic_buf_.substr(0, length - 1);
                dynamic_buf_.erase(0, length);
                do_resolve_domain();
            }
        });
    }

    void do_resolve_domain() {
        auto self(shared_from_this());
        resolver_.async_resolve(socks_info_.domain_name, std::to_string(socks_info_.dst_port),
        [this, self](boost::system::error_code ec, tcp::resolver::results_type results) {
            if (!ec) {
                auto endpoint = *results.begin();
                socks_info_.dst_ip = endpoint.endpoint().address().to_string();
                process_request();
            }
            else {
                // std::cerr << "DNS Resolve failed: " << ec.message() << std::endl;
                do_reply(SOCKS_REJECT, socks_info_.dst_port);
            }
        });
    }

    void process_request() {
        bool pass_check = check_firewall();
        std::string reply_status = pass_check ? "Accept" : "Reject";
        socks_info_.print_info(client_socket_.remote_endpoint(), reply_status);
        if (!pass_check) {
            do_reply(SOCKS_REJECT, socks_info_.dst_port);
            return;
        }

        if (socks_info_.cd == SOCKS_CONNECT) do_connect();
        else if (socks_info_.cd == SOCKS_BIND) do_bind();
    }

    bool check_firewall() {
        std::ifstream file("socks.conf");
        if (!file.is_open()) return false;

        std::string line;
        char target_mode = (socks_info_.cd == SOCKS_CONNECT) ? 'c' : 
                            (socks_info_.cd == SOCKS_BIND) ? 'b' : '?';

        while (std::getline(file, line)) {
            std::stringstream iss(line);
            std::string permit, mode_str, ip_rule;
            
            if (iss >> permit >> mode_str >> ip_rule) {
                if (permit != "permit" || mode_str.empty()) continue;
                if (mode_str[0] != target_mode) continue;

                std::string regex_pattern = "";
                for (char c : ip_rule) {
                    if (c == '.') regex_pattern += "\\.";
                    else if (c == '*') regex_pattern += ".*";
                    else regex_pattern += c;
                }

                std::regex rule_regex(regex_pattern);
                if (std::regex_match(socks_info_.dst_ip, rule_regex)) {
                    return true;
                }
            }
        }
        return false;
    }

    void do_reply(uint8_t reply_code, uint16_t reply_port, std::function<void()> next_step = nullptr) {
        memset(socks_reply_buf_, 0, sizeof(socks_reply_buf_));
        socks_reply_buf_[1] = reply_code;
        socks_reply_buf_[2] = static_cast<uint8_t>(reply_port >> 8);
        socks_reply_buf_[3] = static_cast<uint8_t>(reply_port & 255);
        auto self(shared_from_this());
        boost::asio::async_write(client_socket_, boost::asio::buffer(socks_reply_buf_),
        [this, self, reply_code, next_step](boost::system::error_code ec, std::size_t) {
            if (ec || reply_code != SOCKS_ACCEPT || !next_step) {
                close_sockets();
            }
            else next_step();
        });
    }

    void close_sockets() {
        boost::system::error_code ec;
        if (client_socket_.is_open()) {
            client_socket_.close(ec);
        }
        if (target_socket_.is_open()) {
            target_socket_.close(ec);
        }
        if (bind_acceptor_.is_open()) {
            bind_acceptor_.close(ec);
        }
    }

    void do_connect() {
        auto address = boost::asio::ip::make_address(socks_info_.dst_ip);
        tcp::endpoint target_ep(address, socks_info_.dst_port);
        auto self(shared_from_this());
        target_socket_.async_connect(target_ep,
        [this, self](boost::system::error_code ec) {
            if (!ec) {
                do_reply(SOCKS_ACCEPT, socks_info_.dst_port, [this](){do_relay();});
            } 
            else {
                do_reply(SOCKS_REJECT, socks_info_.dst_port);
            }
        });
    }

    void do_relay() {
        do_read_from_client();
        do_read_from_target();
    }

    void do_read_from_client() {
        auto self(shared_from_this());
        client_socket_.async_read_some(boost::asio::buffer(c2t_buf_, max_length),
        [this, self](boost::system::error_code ec, std::size_t length){
            if (!ec) {
                do_write_to_target(length);
            }
            else {
                close_sockets();
            }
        });
    }

    void do_write_to_target(std::size_t length) {
        auto self(shared_from_this());
        boost::asio::async_write(target_socket_, boost::asio::buffer(c2t_buf_, length),
        [this, self](boost::system::error_code ec, std::size_t){
            if (!ec) {
                do_read_from_client();
            }
            else {
                close_sockets();
            }
        });
    }

    void do_read_from_target() {
        auto self(shared_from_this());
        target_socket_.async_read_some(boost::asio::buffer(t2c_buf_, max_length),
        [this, self](boost::system::error_code ec, std::size_t length){
            if (!ec) {
                do_write_to_client(length);
            }
            else {
                close_sockets();
            }
        });
    }

    void do_write_to_client(std::size_t length) {
        auto self(shared_from_this());
        boost::asio::async_write(client_socket_, boost::asio::buffer(t2c_buf_, length),
        [this, self](boost::system::error_code ec, std::size_t){
            if (!ec) {
                do_read_from_target();
            }
            else {
                close_sockets();
            }
        });
    }

    void do_bind() {
        if (bind_acceptor_.is_open()) {
            close_sockets();
            return;
        }
        bind_acceptor_ = tcp::acceptor(client_socket_.get_executor(), tcp::endpoint(tcp::v4(), 0));
        uint16_t bind_port = bind_acceptor_.local_endpoint().port();

        do_reply(SOCKS_ACCEPT,  bind_port, [this](){do_accept_bind();});
    }

    void do_accept_bind() {
        auto self(shared_from_this());
        bind_acceptor_.async_accept(target_socket_,
        [this, self](boost::system::error_code ec) {
            if (!ec) {
                do_reply(SOCKS_ACCEPT, 0, [this](){do_relay();});
            } 
            else close_sockets();
            bind_acceptor_.close(); 
        });
    }
};
class Server {
public:
    Server(boost::asio::io_context& io_context, short port) 
    : io_context_(io_context), acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
        do_accept();
    }
private:
    boost::asio::io_context& io_context_;
    tcp::acceptor acceptor_;

    void do_accept() {
        acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket accept_socket) {
            if (!ec) {
                io_context_.notify_fork(boost::asio::io_context::fork_prepare);
                pid_t pid = fork();
                if (pid == 0) {
                    io_context_.notify_fork(boost::asio::io_context::fork_child);
                    acceptor_.close();
                    std::make_shared<Session>(std::move(accept_socket))->start();
                }
                else if (pid > 0) {
                    io_context_.notify_fork(boost::asio::io_context::fork_parent);
                    accept_socket.close();
                    do_accept();
                }
                else {
                    // std::cerr << "Error: fork() failed. Connection dropped." << std::endl;
                    io_context_.notify_fork(boost::asio::io_context::fork_parent);
                    accept_socket.close();
                    do_accept();
                }
            }
            else do_accept();
        });
    }
};

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            // std::cerr << "Usage: sock_server <port>\n";
            return 1;
        }

        signal(SIGCHLD, SIG_IGN);
        boost::asio::io_context io_context;
        Server s(io_context, std::atoi(argv[1]));
        io_context.run();
    }
    catch (std::exception& e) {
        // std::cerr << "Exception: " << e.what() << std::endl;
    }
}