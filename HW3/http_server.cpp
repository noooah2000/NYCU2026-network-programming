#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <signal.h>

using boost::asio::ip::tcp;

class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    HttpSession(tcp::socket socket) : accept_socket_(std::move(socket)) {}

    void start() {
        do_read();
    }

private:
    tcp::socket accept_socket_;
    static constexpr int max_length = 1024;
    char data_[max_length];
    std::string request_buffer_;

    std::string request_method_;
    std::string request_uri_;
    std::string query_string_;
    std::string server_protocol_;
    std::string http_host_;

    std::string script_path_;

    void do_read() {
        auto self(shared_from_this());
        accept_socket_.async_read_some(boost::asio::buffer(data_, max_length),
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (ec) return;
            request_buffer_.append(data_, length);
            if (request_buffer_.find("\r\n\r\n") != std::string::npos) {
                parse_request();
            }
            else do_read();
        });
    }

    void parse_request() {
        std::stringstream ss(request_buffer_);
        std::string first_line;

        std::getline(ss, first_line);
        if (!first_line.empty() && first_line.back() == '\r') {
            first_line.pop_back(); 
        }

        std::stringstream first_line_ss(first_line);
        first_line_ss >> request_method_ >> request_uri_ >> server_protocol_;

        size_t pos;
        if ((pos = request_uri_.find("?")) != std::string::npos) {
            query_string_ = request_uri_.substr(pos + 1);
            script_path_ = request_uri_.substr(0, pos);
        }
        else {
            query_string_ = "";
            script_path_ = request_uri_;
        }

        std::string header_line;
        while (std::getline(ss, header_line)) {
            if (!header_line.empty() && header_line.back() == '\r') {
                header_line.pop_back(); 
            }
            if (header_line.empty()) break;

            if (header_line.find("Host: ") == 0) {
                http_host_ = header_line.substr(6);
            }
        }

        std::cerr << "Method: " << request_method_ << std::endl;
        std::cerr << "URI: " << request_uri_ << std::endl;
        std::cerr << "Query: " << query_string_ << std::endl;
        std::cerr << "----------------------------------------" << std::endl;

        execute_cgi(); 
    }

    void execute_cgi() {
        pid_t pid = fork();
        if (pid < 0) {
            std::cerr << "Fork failed" << std::endl;
            std::string error_msg = "HTTP/1.1 500 Internal Server Error\r\n\r\nFork failed.";
            boost::asio::write(accept_socket_, boost::asio::buffer(error_msg));
            return;
        }
        if (pid == 0) {
            setenv("REQUEST_METHOD", request_method_.c_str(), 1);
            setenv("REQUEST_URI", request_uri_.c_str(), 1);
            setenv("QUERY_STRING", query_string_.c_str(), 1);
            setenv("SERVER_PROTOCOL", server_protocol_.c_str(), 1);
            setenv("HTTP_HOST", http_host_.c_str(), 1);

            setenv("SERVER_ADDR", accept_socket_.local_endpoint().address().to_string().c_str(), 1);
            setenv("SERVER_PORT", std::to_string(accept_socket_.local_endpoint().port()).c_str(), 1);

            boost::system::error_code ec;
            auto remote_ep = accept_socket_.remote_endpoint(ec);
            if (!ec) {
                setenv("REMOTE_ADDR", remote_ep.address().to_string().c_str(), 1);
                setenv("REMOTE_PORT", std::to_string(remote_ep.port()).c_str(), 1);
            } 
            else {
                setenv("REMOTE_ADDR", "0.0.0.0", 1);
                setenv("REMOTE_PORT", "0", 1);
            }

            int sock_fd = accept_socket_.native_handle();
            dup2(sock_fd, STDIN_FILENO);
            dup2(sock_fd, STDOUT_FILENO);
            dup2(sock_fd, STDERR_FILENO);
            close(sock_fd);

            std::cout << "HTTP/1.1 200 OK\r\n" << std::flush;

            std::string exec_path = "." + script_path_;
            if (execlp(exec_path.c_str(), exec_path.c_str(), nullptr) < 0) {
                std::cerr << "Exec cgi Fail" << std::endl;
                exit(1);
            }
        }
        else {
            accept_socket_.close();
        }
    }
};
class Server {
public:
    Server(boost::asio::io_context& io_context, short port) 
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
        do_accept();
    }
private:
    tcp::acceptor acceptor_;

    void do_accept() {
        acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::make_shared<HttpSession>(std::move(socket))->start();
            }
            do_accept();
        });
    }
};

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: http_server <port>\n";
            return 1;
        }

        signal(SIGCHLD, SIG_IGN);
        signal(SIGPIPE, SIG_IGN);
        boost::asio::io_context io_context;
        Server s(io_context, std::atoi(argv[1]));
        io_context.run();
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}