#include <iostream>
#include <boost/asio.hpp>
#include <boost/system/error_code.hpp>
#include <thread>
using boost::asio::ip::tcp;

struct Request {
  ::int32_t index;
  float min_dist;
  float max_dist;
};

struct Reply {
  ::int32_t index;
  float dist;
};

class tcp_connection : public std::enable_shared_from_this<tcp_connection>{
public:
  static std::shared_ptr<tcp_connection> create(boost::asio::io_context& io_context) {
    return std::shared_ptr<tcp_connection>(new tcp_connection(io_context));
  }

  tcp::socket& socket() {
    return socket_;
  }

  void start() {
    std::cout << "starting read" << std::endl;
    boost::asio::async_read(socket_, boost::asio::buffer(buff), std::bind(&tcp_connection::handle_read, shared_from_this(),
      boost::asio::placeholders::error,
      boost::asio::placeholders::bytes_transferred));
  }

private:
  void handle_read(const boost::system::error_code& error,
    size_t bytes_transferred)
  {
    if(!error) {
      get_send();
      std::cout << "starting write" << std::endl;
      boost::asio::async_write(socket_, boost::asio::buffer(repl_buff, sizeof(Reply)), std::bind(&tcp_connection::handle_write, shared_from_this(),
        boost::asio::placeholders::error,
        boost::asio::placeholders::bytes_transferred));
    }
  }

  void handle_write(const boost::system::error_code& error,
    size_t bytes_transferred)
  {
  }

  void get_send() {
    repl_buff[0].index = (rand() % 10);
    repl_buff[0].dist = (buff[0].max_dist - buff[0].min_dist) * rand() / RAND_MAX + buff[0].min_dist;
  }

  tcp_connection(boost::asio::io_context& io_context) : socket_(io_context) {
  }

  std::array<Request, 1> buff;
  std::array<Reply, 1> repl_buff;
  tcp::socket socket_;
};

class tcp_server {
public:
  tcp_server(boost::asio::io_context& io_context) : io_context_(io_context),
    acceptor_(io_context_, tcp::endpoint(tcp::v4(), 500)) {
    start_accept();
  }

private:
  void start_accept() {
    std::cout << "Server listening for requests" << std::endl;
    std::shared_ptr<tcp_connection> conn = tcp_connection::create(io_context_);
    acceptor_.async_accept(conn->socket(), std::bind(&tcp_server::handle_accept, this, boost::asio::placeholders::error, conn));
  }

  void handle_accept(const boost::system::error_code& error, std::shared_ptr<tcp_connection> conn) {
    std::cout << "accepted a connection" << std::endl;
    if(!error) {
      conn->start();
    }
    start_accept();
  }

  boost::asio::io_context& io_context_;
  tcp::acceptor acceptor_;
};

int main() {
  boost::asio::io_context io;
  tcp_server server(io);
  std::cout << "Running io context" << std::endl;
  std::thread thr([&io](){io.run();});
  thr.join();
}