/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

//RYCB
//gRPC server implementation to send validation messages to the gRPC node.js libp2p implemntation
//We do that to stdy the impact of gossipsub on the spread of validation messages

#include <memory>
#include <iostream>
#include <string>
#include <thread>

#include <grpcpp/grpcpp.h>
#include <grpc/support/log.h>

#include "gossip_message.grpc.pb.h"

using grpc::Server;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerCompletionQueue;
using grpc::Status;
using gossipmessage::GossipMessage;
using gossipmessage::Gossip;
using gossipmessage::Control;



class GossipMessageImpl final : public GossipMessage::AsyncService
{
	public:

	Status Exchange(ServerContext* context, ServerReader<Gossip>* reader,
                     Control* control) override 
	{
	    Gossip gossip;
	    while (stream->Read(&gossip)) 
	    {
	      //process the reading
	    
	    }
	    control = 1;
	    return Status::OK;
	  }

 private:
  std::mutex mu_;
  std::vector<RouteNote> received_notes_;

}
// class ServerImpl final {
//  public:
//   ~ServerImpl() {
//     server_->Shutdown();
//     // Always shutdown the completion queue after the server.
//     cq_->Shutdown();
//   }

//   // There is no shutdown handling in this code.
//   // The server receives messages
//  void Connect() {
//     std::string server_address("0.0.0.0:50051");
//     ServerBuilder builder;
//     // Listen on the given address without any authentication mechanism.
//     builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
//     // Register "service_" as the instance through which we'll communicate with
//     // clients. In this case it corresponds to an *asynchronous* service.
//     builder.RegisterService(&service_);
//     cq_ = builder.AddCompletionQueue();
//     server_ = builder.BuildAndStart();
//     std::cout << "gRPC listening on " << server_address << std::endl;

//     // Proceed to the server's main loop.
//     // HandleRpcs();
//   }



//  private:
//   // Class encompasing the state and logic needed to serve a request.
//   class CallData {
//    public:
//     // Take in the "service" instance (in this case representing an asynchronous
//     // server) and the completion queue "cq" used for asynchronous communication
//     // with the gRPC runtime.
//     CallData(GossipMessage::AsyncService* service, ServerCompletionQueue* cq)
//         : service_(service), cq_(cq), responder_(&ctx_), status_(CREATE) {
//       // Invoke the serving logic right away.
//       Proceed();
//     }

//     void Proceed() {
//       if (status_ == CREATE) {
//         // Make this instance progress to the PROCESS state.
//         status_ = PROCESS;

//         // As part of the initial CREATE state, we *request* that the system
//         // start processing Receive requests. In this request, "this" acts are
//         // the tag uniquely identifying the request (so that different CallData
//         // instances can serve different requests concurrently), in this case
//         // the memory address of this CallData instance.
//         service_->RequestReceive(&ctx_, &request_, &responder_, cq_, cq_,
//                                   this);
//       } else if (status_ == PROCESS) {
//         // Spawn a new CallData instance to serve new clients while we process
//         // the one for this CallData. The instance will deallocate itself as
//         // part of its FINISH state.
//         new CallData(service_, cq_);

//         // The actual processing.
//         std::string prefix("Hello ");
//         reply_.set_message(prefix + request_.name());

//         // And we are done! Let the gRPC runtime know we've finished, using the
//         // memory address of this instance as the uniquely identifying tag for
//         // the event.
//         status_ = FINISH;
//         responder_.Finish(reply_, Status::OK, this);
//       } else {
//         GPR_ASSERT(status_ == FINISH);
//         // Once in the FINISH state, deallocate ourselves (CallData).
//         delete this;
//       }
//     }

//    private:
//     // The means of communication with the gRPC runtime for an asynchronous
//     // server.
//     GossipMessage::AsyncService* service_;
//     // The producer-consumer queue where for asynchronous server notifications.
//     ServerCompletionQueue* cq_;
//     // Context for the rpc, allowing to tweak aspects of it such as the use
//     // of compression, authentication, as well as to send metadata back to the
//     // client.
//     ServerContext ctx_;

//     // What we get from the client.
//     Control request_;
//     // What we send back to the client.
//     Gossip reply_;

//     // The means to get back to the client.
//     ServerAsyncResponseWriter<Gossip> responder_;

//     // Let's implement a tiny state machine with the following states.
//     enum CallStatus { CREATE, PROCESS, FINISH };
//     CallStatus status_;  // The current serving state.
//   };


//   std::unique_ptr<ServerCompletionQueue> cq_;
//   GossipMessage::AsyncService service_;
//   std::unique_ptr<Server> server_;
// };


// To initiate the server

// int main(int argc, char** argv) {
//   ServerImpl server;
//   server.Run();

//   return 0;
// }




// #include <grpcpp/grpcpp.h>

// #include <org/xrpl/rpc/v1/validation_message.grpc.pb.h>

// #include <grpc/grpc.h>
// #include <grpcpp/server.h>
// #include <grpcpp/server_builder.h>
// #include <grpcpp/server_context.h>
// #include <grpcpp/security/server_credentials.h>

// using grpc::Server;
// using grpc::ServerBuilder;
// using grpc::ServerContext;
// using grpc::ServerReader;
// using grpc::ServerReaderWriter;
// using grpc::ServerWriter;
// using grpc::Status;
// using validationmessage::ValidationMess;
// using std::chrono::system_clock;


// class ValidMessagImpl final : public RouteGuide::AsyncService {

// }

// void RunServer(const std::string& db_path) {
//   std::string server_address("0.0.0.0:50051");
//   RouteGuideImpl service(db_path);

//   ServerBuilder builder;
//   builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
//   builder.RegisterService(&service);
//   std::unique_ptr<Server> server(builder.BuildAndStart());
//   std::cout << "Server listening on " << server_address << std::endl;
//   server->Wait();
// }