#include <iostream>
#include <grpcpp/grpcpp.h>
#include <thread>
#include <boost/asio.hpp>
#include <random>
#include <chrono>
#include "messages.grpc.pb.h"
#include "messages.pb.h"

using grpc::Channel;
using boost::asio::ip::tcp;

struct tcp_init_req {
  int type;
  int32_t id;
};

class tcp_proxy_conn : public std::enable_shared_from_this<tcp_proxy_conn> {
public:
  static std::shared_ptr<tcp_proxy_conn> create(boost::asio::io_context& io, int32_t id, 
          std::string data,
          std::function<void(std::string, bool)> callback) {
    std::cout << "creating asio client" << std::endl;
    return std::shared_ptr<tcp_proxy_conn>(new tcp_proxy_conn(io, id, data, callback));
  }

  void start_store() {
    type_buff[0].type = 1;
    type_buff[0].id = id_;
    boost::asio::async_write(socket_, boost::asio::buffer(type_buff, sizeof(tcp_init_req)),
      [&](const boost::system::error_code& error, size_t /*bytes_transferred*/) {
        boost::asio::async_write(socket_, boost::asio::buffer(data_, 150),
          std::bind(&tcp_proxy_conn::store_write_handler, shared_from_this(),
          boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
      });
  }

  void start_read() {
    type_buff[0].type = 0;
    type_buff[0].id = id_;
    boost::asio::async_write(socket_, boost::asio::buffer(type_buff, sizeof(tcp_init_req)),
          std::bind(&tcp_proxy_conn::read_write_handler, shared_from_this(),
            boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
  }

private:
  void store_read_handler(const boost::system::error_code& error,
      size_t /*bytes_transferred*/) {
    if(!error && store_repl_buff[0]) {
      std::cout << "got tcp response for store" << std::endl;
      callback_("", false);
    } else {
      if(error)
        std::cout << error.what() << std::endl;
      std::cout << "error" << std::endl;
      callback_("", true);
    }
  }

  void store_write_handler(const boost::system::error_code& error,
      size_t /*bytes_transferred*/) {
    std::cout << "sent store request" << std::endl;
    boost::asio::async_read(socket_, boost::asio::buffer(store_repl_buff), std::bind(&tcp_proxy_conn::store_read_handler, 
      shared_from_this(),
      boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
  }

  void read_read_handler(const boost::system::error_code& error,
      size_t bytes_transferred) {
    if(!error) {
      std::cout << "got tcp response for read" << std::endl;
      callback_(data_.data(), false);
    } else {
      std::cout << error.what() << std::endl;
      std::cout << "error" << std::endl;
      callback_("", true);
    }
  }

  void read_write_handler(const boost::system::error_code& error,
      size_t /*bytes_transferred*/) {
    boost::asio::async_read(socket_, boost::asio::buffer(data_, 150), std::bind(&tcp_proxy_conn::read_read_handler, 
      shared_from_this(),
      boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
  }

  tcp_proxy_conn(boost::asio::io_context& io, int32_t id, std::string data,
      std::function<void(std::string, bool)> callback) : socket_(io), callback_(callback), id_(id){
    cpy_data(data);
    try {
      tcp::resolver rslv(io);
      tcp::resolver::results_type endpoints = rslv.resolve("127.0.0.1", "500");
      boost::asio::connect(socket_, endpoints);
    } catch (std::exception& e) {
      std::cerr << e.what() << std::endl;
      exit(1);
    }
  }

  void cpy_data(std::string data) {
    for(int i = 0; data[i] != '\0'; i++) {
      data_[i]= data[i];
    }
  }

  std::function<void(std::string, bool)> callback_;
  tcp::socket socket_;
  std::array<char, 150> data_;
  std::array<tcp_init_req, 1> type_buff;
  std::array<bool, 1> store_repl_buff;
  int32_t id_;
};

class Player {
public:
  enum State {
    Created,
    Active
  };

  Player() {
    id = ++last_id;
    state = Created;
  }

  ~Player() {}

  State get_state() {
    return state;
  }

  int get_id() {
    return id;
  }

  void start() {
    state = Active;
  }

  void stop() {
    state = Created;
  }

  static int32_t last_id;
private:
  State state;
  int32_t id;
};

int32_t Player::last_id;

typedef Server::WithCallbackMethod_Store<Server::WithCallbackMethod_Read<Server::Service > > MyService;
class Proxy final : public MyService {
public:
  explicit Proxy(boost::asio::io_context& io) :io_(io) {
  }

  grpc::Status Create(grpc::ServerContext* context, const empt* req,
      Id* reply) override {
    Player* newPlayer = new Player();
    players[newPlayer->get_id()] = newPlayer;
    reply->set_id(newPlayer->get_id());
    return grpc::Status::OK;
  }

  grpc::Status Delete(grpc::ServerContext* context, const Id* req,
      empt* repl) override {
    try {
      Player& plyr = *players.at(req->id());
      players.erase(req->id());
      plyr.~Player();
      return grpc::Status::OK;
    } catch(std::out_of_range()) {
      return grpc::Status::CANCELLED;
    }
  }

  grpc::Status Start(grpc::ServerContext* context, const Id* req, 
      empt* repl) override {
    try {
      Player& plyr = *players.at(req->id());
      plyr.start();
      return grpc::Status::OK;
    } catch(std::out_of_range()) {
      return grpc::Status::CANCELLED;
    }
  }

  grpc::Status Stop(grpc::ServerContext* context, const Id* req, 
      empt* repl) override {
    try {
      Player& plyr = *players.at(req->id());
      plyr.stop();
      return grpc::Status::OK;
    } catch(std::out_of_range()) {
      return grpc::Status::CANCELLED;
    }
  }

  grpc::ServerUnaryReactor* Store(grpc::CallbackServerContext* context, const Message* request,
      empt* reply) override {
    class StoreMess: public grpc::ServerUnaryReactor {
    public:
      StoreMess(const Message* request, boost::asio::io_context& io_, std::unordered_map<int, Player*>& players) : req_(*request) {
        try {
          Player& plyr = *players.at(req_.id());
          if(plyr.get_state() != Player::State::Active)
            Finish(grpc::Status::CANCELLED);
          else {
            std::cout << "got request with " << request->data() << std::endl;
            conn = tcp_proxy_conn::create(io_, request->id(), request->data(), [this](std::string mess, bool error){tcp_callback(error);});
            conn->start_store();
          }
        } catch (std::out_of_range){
          Finish(grpc::Status::CANCELLED);
        }
      }

      void OnDone() override {
        std::cout << "RPC Completed" << std::endl;
        delete this;
      }

      void OnCancel() override { std::cerr << "RPC Cancelled" << std::endl; }

    private:
      void tcp_callback(bool error) {
        std::cout << "starting write on GRPC" << std::endl;
        if(!error) {
          Finish(grpc::Status::OK);
        } else {
          Finish(grpc::Status::CANCELLED);
        }
      }

      std::shared_ptr<tcp_proxy_conn> conn;
      const Message& req_;
    };
    return new StoreMess(request, io_, players);
  }

  grpc::ServerUnaryReactor* Read(grpc::CallbackServerContext* context, const Id* request,
      Message* reply) override {
    class ReadMess: public grpc::ServerUnaryReactor {
    public:
      ReadMess(const Id* request, Message* reply, boost::asio::io_context& io_, std::unordered_map<int, Player*>& players) : req_(*request), repl_(*reply) {
        try {
          Player& plyr = *players.at(req_.id());
          if(plyr.get_state() != Player::State::Active)
            Finish(grpc::Status::CANCELLED);
          else {
            conn = tcp_proxy_conn::create(io_, request->id(), std::string(""), [this](std::string mess, bool error) {tcp_callback(mess, error); });
            conn->start_read();
          }
        } catch (std::out_of_range){
          Finish(grpc::Status::CANCELLED);
        }
      }

      void OnDone() override {
        std::cout << "RPC Completed" << std::endl;
        delete this;
      }

      void OnCancel() override { std::cerr << "RPC Cancelled" << std::endl; }

    private:
      void tcp_callback(std::string mess, bool error) {
        std::cout << "starting write on GRPC" << std::endl;
        if(!error) {
          repl_.set_data(mess);
          repl_.set_id(req_.id());
          Finish(grpc::Status::OK);
        } else {
          Finish(grpc::Status::CANCELLED);
        }
      }

      std::shared_ptr<tcp_proxy_conn> conn;
      const Id& req_;
      Message& repl_;
    };
    return new ReadMess(request, reply, io_, players);
  }
private:
  boost::asio::io_context& io_;
  std::unordered_map<int, Player*> players;
};

int main() {
  unsigned seed1 = std::chrono::system_clock::now().time_since_epoch().count();
  srand(seed1);
  Player::last_id = rand();
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