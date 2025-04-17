#include <grpcpp/grpcpp.h>
#include <grpcpp/channel.h>
#include <memory>
#include <iostream>

#include "messages.grpc.pb.h"

using grpc::Channel;

class Client {
public:
  Client(std::shared_ptr<Channel> channel) : stub_(Server::NewStub(channel)) {}

  void getClosest(Request dot) {
    grpc::ClientContext context;
    Reply result;
    grpc::Status status = stub_->get_closest(&context, dot, &result);
    if (!status.ok())
      std::cout << "error during request" << std::endl;
    else
      std::cout << "found: " << result.index() << " with distance " << result.dist() << std::endl;
  }
private:
  std::unique_ptr<Server::Stub> stub_;
};

int main() {
  Client client(grpc::CreateChannel("localhost:50500", grpc::InsecureChannelCredentials()));
  Request dot;
  dot.set_index(1);
  dot.set_min_dist(11.4f);
  dot.set_max_dist(73.4f);
  client.getClosest(dot);
  return 0;
}