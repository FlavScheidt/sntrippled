#ifndef GOSSIP_MESSAGE_H
#define GOSSIP_MESSAGE_H


#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include <ripple/overlay/Message.h>

#include "org/xrpl/rpc/v1/gossip_message.grpc.pb.h"


using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using gossipmessage::Gossip;
using gossipmessage::Control;
using gossipmessage::GossipMessage;

namespace gossipClient
{
	class GossipMessageClient 
	{

	    public:
	    GossipMessageClient(std::shared_ptr<Channel> channel);

	    int toLibP2P(std::shared_ptr<ripple::Message> const& m, ripple::compression::Compressed compressionEnabled_);

	    // std::string bufferToString(std::vector<unsigned char> const input);

	    private:
	    std::unique_ptr<GossipMessage::Stub> stub_;
	};
}

#endif