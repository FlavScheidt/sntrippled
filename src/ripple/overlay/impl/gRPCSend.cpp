#include <ripple/overlay/gRPCSend.h>

//RYCB
//Send transactions to the libp2p, acting as the client, if we take
//the pov of the gRPC tutorials
    std::string
    bufferToString(std::vector<unsigned char> const input)
    {
        std::string output;
        for (char c: input)
            output.push_back(c);

        return output;
    }

namespace gossipClient
{
    GossipMessageClient::GossipMessageClient(std::shared_ptr<Channel> channel)
          : stub_(GossipMessage::NewStub(channel)) 
    {
    }

    int 
    GossipMessageClient::toLibP2P(std::shared_ptr<ripple::Message> const& m, ripple::compression::Compressed compressionEnabled_) 
    {
        // Data we are sending to the server.
        Gossip gossip;
        //RYCB
        //Need to extract the bytes from the message
        //Then transform the bytes in a string (it seems so ugly)
        std::vector<unsigned char> const _buff = m->getBuffer(compressionEnabled_);
        std::string _buffer = bufferToString(_buff);
        gossip.set_message(_buffer);

        // Container for the data we expect from the server.
        Control control;

        // Context for the client. It could be used to convey extra information to
        // the server and/or tweak certain RPC behaviors.
        ClientContext context;

        // Overwrite the call's compression algorithm to DEFLATE.
        // context.set_compression_algorithm(GRPC_COMPRESS_DEFLATE);

        // The actual RPC.
        Status status = stub_->toLibP2P(&context, gossip, &control);

        // Act upon its status.
        if (status.ok()) 
        {
            // if (auto stream = journal_.trace())
            // {
            //     stream << "Validation Sent via gRPC succesfully";
            // }
            std::cout << "Validation Sent via gRPC succesfully\n";
            return 0;
        } 
        else 
        {
            // if (auto stream = journal_.trace())
            // {
            //     stream << status.error_code() << ": " << status.error_message()
            //         << std::endl;
            // }
            std::cout << status.error_code() << ": " << status.error_message() << std::endl;
            return 1;
        }
    }
}