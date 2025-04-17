#include <iostream>
#include <grpcpp/grpcpp.h>
#include <thread>
#include <boost/asio.hpp>
#include "messages.grpc.pb.h"
#include "messages.pb.h"

using grpc::Channel;
using boost::asio::ip::tcp;

struct tcp_Request {
  ::int32_t index;
  float min_dist;
  float max_dist;
};

struct tcp_Reply {
  ::int32_t index;
  float dist;
};
class tcp_proxy_conn : public std::enable_shared_from_this<tcp_proxy_conn> {
public:
  static std::shared_ptr<tcp_proxy_conn> create(boost::asio::io_context& io, Request r, std::function<void(tcp_Reply,bool)> callback) {
    std::cout << "creating asio client" << std::endl;
    return std::shared_ptr<tcp_proxy_conn>(new tcp_proxy_conn(io, r, callback));
  }

  void start() {
    boost::asio::async_write(socket_, boost::asio::buffer(buff, sizeof(tcp_Request)), std::bind(&tcp_proxy_conn::write_handler, shared_from_this(),
      boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
  }

  void read() {
    boost::asio::async_read(socket_, boost::asio::buffer(repl_buff), std::bind(&tcp_proxy_conn::handler, shared_from_this(),
      boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
  }

private:
  void handler(const boost::system::error_code& error,
    size_t /*bytes_transferred*/) {
    if(!error) {
      std::cout << "got tcp response" << std::endl;
      callback_(repl_buff[0], false);
    } else {
      std::cout << error.what() << std::endl;
      std::cout << "error" << std::endl;
      callback_(repl_buff[0], true);
    }
  }

  void write_handler(const boost::system::error_code& error,
    size_t /*bytes_transferred*/) {
    read();
  }

  tcp_proxy_conn(boost::asio::io_context& io, Request r, std::function<void(tcp_Reply, bool)> callback) : socket_(io), callback_(callback) {
    buff[0].index = r.index();
    buff[0].min_dist = r.min_dist();
    buff[0].max_dist = r.max_dist();
    try {
      tcp::resolver rslv(io);
      tcp::resolver::results_type endpoints = rslv.resolve("127.0.0.1", "500");
      boost::asio::connect(socket_, endpoints);
    } catch (std::exception& e) {
      std::cerr << e.what() << std::endl;
      exit(1);
    }
  }

  std::function<void(tcp_Reply, bool)> callback_;
  tcp::socket socket_;
  std::array<tcp_Request, 1> buff;
  std::array<tcp_Reply, 1> repl_buff;
};

class Proxy final : public Server::CallbackService {
public:
  explicit Proxy(boost::asio::io_context& io) :io_(io) {
  }
  grpc::ServerUnaryReactor* get_closest(grpc::CallbackServerContext* context, const Request* request,
    Reply* reply) {
    class SendReplies: public grpc::ServerUnaryReactor {
    public:
      SendReplies(const Request* request, Reply& reply, boost::asio::io_context& io_) : req_(*request), reply_(reply){
        conn = tcp_proxy_conn::create(io_, *request, [this](tcp_Reply rep, bool error){nextWrite(rep, error);});
        conn->start();
      }

      void OnDone() override {
        std::cout << "RPC Completed" << std::endl;
        delete this;
      }

      void OnCancel() override { std::cerr << "RPC Cancelled" << std::endl; }
    private:
      void nextWrite(tcp_Reply rep, bool error) {
        std::cout << "starting write on GRPC" << std::endl;
        if(!error) {
          reply_.set_index(rep.index);
          reply_.set_dist(rep.dist);
          Finish(grpc::Status::OK);
        } else {
          Finish(grpc::Status::CANCELLED);
        }
      }

      std::shared_ptr<tcp_proxy_conn> conn;
      Reply& reply_;
      Request req_;
    };
    return new SendReplies(request, *reply, io_);
  }
private:
  boost::asio::io_context& io_;
};

int main() {
  boost::asio::io_context io;
  std::string server_adress("localhost:50500");
  Proxy service(io);
  boost::asio::executor_work_guard wguard = boost::asio::make_work_guard(io);
  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_adress, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  std::thread tread([&io](){io.run();});
  server->Wait();
  tread.join();
  return 0;
}