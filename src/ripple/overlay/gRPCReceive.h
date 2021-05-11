#ifndef GOSSIP_MESSAGE_RECEIVE_H
#define GOSSIP_MESSAGE_RECEIVE_H

#include <memory>
#include <iostream>
#include <string>
#include <thread>
#include <pthread.h>
#include <grpc++/grpc++.h>
#include <sys/types.h>
#include <boost/lexical_cast.hpp>  

#include <ripple/overlay/Compression.h>

#include <boost/circular_buffer.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/optional.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <cstdint>
#include <queue>

// #include <grpc/grpc.h>
// #include <grpcpp/server.h>
// #include <grpcpp/server_builder.h>
// #include <grpcpp/server_context.h>
// #include <grpcpp/security/server_credentials.h>

// #include <ripple/overlay/Message.h>

#include "org/xrpl/rpc/v1/gossip_message.grpc.pb.h"

#include <iostream>
#include <boost/asio/buffers_iterator.hpp>
#include <boost/beast.hpp>

using boost::beast::multi_buffer;

using grpc::Server;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerCompletionQueue;
using grpc::Status;

using boost::lexical_cast;

namespace gossipServer
{

    void * Run(void * ret);

    class GossipMessageImpl final 
    {
        public:
        ~GossipMessageImpl();
        GossipMessageImpl();

        // void Run();

        void  ConnectAndRun(void * upperObject);

        // Status toRippled(ServerContext* context, const Gossip* gossip, Control* control);

        private:
        class CallData 
        {
            public:
            CallData(GossipMessage::AsyncService* service, ServerCompletionQueue* cq, void * upperObject);
            void Proceed(void * upperObject);

            boost::beast::multi_buffer read_buffer_grpc;

            private:
            // The means of communication with the gRPC runtime for an asynchronous
            // server.
            GossipMessage::AsyncService* service_;
            // The producer-consumer queue where for asynchronous server notifications.
            ServerCompletionQueue* cq_;
            // Context for the rpc, allowing to tweak aspects of it such as the use
            // of compression, authentication, as well as to send metadata back to the
            // client.
            ServerContext ctx_;
            // What we get from the client.
            Gossip gossip;
            // What we send back to the client.
            Control control;
            // The means to get back to the client.
            ServerAsyncResponseWriter<Control> responder_;
            // Let's implement a tiny state machine with the following states.
            enum CallStatus { CREATE, PROCESS, FINISH };
            CallStatus status_;  // The current serving state.
        };

        void HandleRpcs(void * upperObject);

        std::unique_ptr<ServerCompletionQueue> cq_;
        GossipMessage::AsyncService service_;
        std::unique_ptr<Server> server_;

        std::string gRPCport;

        // beast::WrappedSink sink_;
        // beast::WrappedSink p_sink_;
        // beast::Journal const journal_;
        // beast::Journal const p_journal_;

        // public:
        // beast::Journal const&
        // pjournal() const
        // {
        //     return p_journal_;
        // }  

    };
}

#endif