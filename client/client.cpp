#include <grpcpp/grpcpp.h>
#include <grpcpp/channel.h>
#include <memory>
#include <iostream>

#include "messages.grpc.pb.h"

using grpc::Channel;

class Client {
public:
  Client(std::shared_ptr<Channel> channel) : stub_(Server::NewStub(channel)) {}

  Id Create() {
    grpc::ClientContext context;
    empt req;
    Id id;
    grpc::Status status = stub_->Create(&context, req, &id);
    if (!status.ok())
      std::cout << id.id() << "error during request" << std::endl;
    else
      std::cout << id.id() << "create request successfull: " << std::endl;
    return id;
  }

  void Delete(Id id) {
    grpc::ClientContext context;
    empt repl;
    grpc::Status status = stub_->Delete(&context, id, &repl);
    if (!status.ok())
      std::cout << id.id() << "error during request" << std::endl;
    else
      std::cout << id.id() << "delete request successfull: " << std::endl;
  }

  void start(Id id) {
    grpc::ClientContext context;
    empt repl;
    grpc::Status status = stub_->Start(&context, id, &repl);
    if (!status.ok())
      std::cout << id.id() << "error during request" << std::endl;
    else
      std::cout << id.id() << "start request successfull: " << std::endl;
  }

  void stop(Id id) {
    grpc::ClientContext context;
    empt repl;
    grpc::Status status = stub_->Stop(&context, id, &repl);
    if (!status.ok())
      std::cout << id.id() << "error during request" << std::endl;
    else
      std::cout << id.id() << "stop request successfull: " << std::endl;
  }

  void store(Message mess) {
    grpc::ClientContext context;
    empt repl;
    grpc::Status status = stub_->Store(&context, mess, &repl);
    if (!status.ok())
      std::cout << mess.id() << "error during request" << std::endl;
    else
      std::cout << mess.id() << "store request successfull" << std::endl;
  }

  void read(Id id) {
    grpc::ClientContext context;
    Message repl;
    grpc::Status status = stub_->Read(&context, id, &repl);
    if (!status.ok())
      std::cout << id.id() << "error during request" << std::endl;
    else
      std::cout << id.id() << "request successfull got reply: " << repl.data() << std::endl;
  }

private:
  std::unique_ptr<Server::Stub> stub_;
};

void test(Client* client) {
  Id id = client->Create();
  Message m;
  m.set_id(id.id());
  m.set_data("hello");
  client->start(id);
  client->store(m);
  client->stop(id);
  client->start(id);
  client->read(id);
  m.set_data("hello2");
  client->store(m);
  client->read(id);
  client->stop(id);
  client->Delete(id);
  client->store(m);
  client->read(id);
  client->stop(id);
  client->start(id);
  client->Delete(id);
  id = client->Create();
  client->start(id);
  client->read(id);
}


int main() {
  Client client(grpc::CreateChannel("localhost:50500", grpc::InsecureChannelCredentials()));
  Id id = client.Create();
  Id id2 = client.Create();
  Message m;
  m.set_id(id.id());
  m.set_data("hello");
  Message m2;
  m2.set_id(id2.id());
  m2.set_data("very very very long long string that is too long");
  client.store(m);
  client.start(id);
  {
    client.store(m2);
    client.start(id2);
    client.store(m2);
    client.stop(id2);
    client.read(id2);
    client.start(id2);
    client.read(id2);
  }
  client.store(m);
  client.stop(id);
  client.read(id);
  client.start(id);
  client.read(id);
  test(&client);
  return 0;
}