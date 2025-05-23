#include <iostream>
#include <boost/asio.hpp>
#include <boost/system/error_code.hpp>
#include <thread>
#include "sqlite3.h"

using boost::asio::ip::tcp;

struct tcp_init_req {
  int type;
  int id;
};

class tcp_connection : public std::enable_shared_from_this<tcp_connection>{
public:
  static std::shared_ptr<tcp_connection> create(boost::asio::io_context& io_context, sqlite3* db) {
    return std::shared_ptr<tcp_connection>(new tcp_connection(io_context, db));
  }

  tcp::socket& socket() {
    return socket_;
  }

  void start() {
    std::cout << "starting read" << std::endl;
    boost::asio::async_read(socket_, boost::asio::buffer(type_buff), std::bind(&tcp_connection::handle_read, shared_from_this(),
      boost::asio::placeholders::error,
      boost::asio::placeholders::bytes_transferred));
  }

  void send_read_mess(char* text) {
    for(int i=0; text[i] != '\0'; i++)
      data[i] = text[i];
    boost::asio::async_write(socket_, boost::asio::buffer(data, 150), std::bind(&tcp_connection::handle, shared_from_this(),
      boost::asio::placeholders::error,
      boost::asio::placeholders::bytes_transferred));
  }


private:
  void send_store_mess() {
    boost::asio::async_write(socket_, boost::asio::buffer(store_repl_buff), std::bind(&tcp_connection::handle, shared_from_this(),
      boost::asio::placeholders::error,
      boost::asio::placeholders::bytes_transferred));
  }

  void handle(const boost::system::error_code& error,
    size_t bytes_transferred) {
  }

  void handle_read(const boost::system::error_code& error,
    size_t bytes_transferred)
  {
    if(!error) {
      if(type_buff[0].type == 0) {
        std::cout << "read" << std::endl;
        read_repy();
      } else if(type_buff[0].type == 1) {
        std::cout << "store" << std::endl;
        boost::asio::async_read(socket_, boost::asio::buffer(data, 150), 
          std::bind(&tcp_connection::handle_store_read, shared_from_this(),
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));
      }
    }
  }


  void read_repy()
  {
    char* errmsg;
    sqlite3_exec(db_, std::format("SELECT MESSAGE FROM MESSAGES WHERE ID = {}", type_buff[0].id).data(), 
      [](void* conptr, int nrow, char** mess, char**){
        tcp_connection* conn = reinterpret_cast<tcp_connection*>(conptr);
        if(nrow != 1) {
          std::cerr << "Database error" << std::endl;
          exit(1);
        }
        conn->send_read_mess(mess[0]);
        return 0;
      }, std::move(this), &errmsg);
  }

  void handle_store_read(const boost::system::error_code& error,
    size_t bytes_transferred)
  {
    std::cout << "read store request " << data.data() << std::endl;
    char* errmsg;
    int res = sqlite3_exec(db_, std::format("INSERT INTO MESSAGES(ID, MESSAGE) VALUES ({}, '{}');",
                        type_buff[0].id, data.data()).data(), 
                    NULL, 0, &errmsg);
    if(res == 19) {
      res = sqlite3_exec(db_, std::format("UPDATE MESSAGES SET MESSAGE = '{}' WHERE ID = {};",
                        data.data(), type_buff[0].id).data(), 
                    NULL, 0, &errmsg);
    }
    if(res != SQLITE_OK) {
      std::cerr << "Database error: " << errmsg << std::endl;
      store_repl_buff[0] = 0;
    } else
      store_repl_buff[0] = 1;
    send_store_mess();
  }

  tcp_connection(boost::asio::io_context& io_context, sqlite3* db) : socket_(io_context), db_(db) {
  }

  std::array<char, 150> data;
  std::array<bool, 1> store_repl_buff;
  std::array<tcp_init_req, 1> type_buff;
  tcp::socket socket_;
  sqlite3* db_;
};

class tcp_server {
public:
  tcp_server(boost::asio::io_context& io_context) : io_context_(io_context),
    acceptor_(io_context_, tcp::endpoint(tcp::v4(), 500)) {
    open_db();
    start_accept();
  }

private:
  void open_db() {
    int res = sqlite3_open("data.db", &db);
    if(res) {
      std::cerr << "could not open database" << std::endl;
      exit(1);
    }
    create_table();
  }

  void create_table() {
    char *errmsg=0;
    int res = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS MESSAGES(" \
                      "ID INT PRIMARY KEY NOT NULL," \
                      "MESSAGE TEXT NOT NULL);", NULL, 0, &errmsg);
    if(res != SQLITE_OK ){
      std::cerr << "Database error could not create table" << std::endl;
      std::cout << errmsg << std::endl;
      sqlite3_free(errmsg);
      exit(1);
    }
  }

  void start_accept() {
    std::cout << "Server listening for requests" << std::endl;
    std::shared_ptr<tcp_connection> conn = tcp_connection::create(io_context_, db);
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
  sqlite3* db;
};

int main() {
  boost::asio::io_context io;
  tcp_server server(io);
  std::cout << "Running io context" << std::endl;
  std::thread thr([&io](){io.run();});
  thr.join();
}