#include <ripple/overlay/gRPCSend.h>

#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/insert_linebreaks.hpp>
#include <boost/archive/iterators/remove_whitespace.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/archive/iterators/ostream_iterator.hpp>
#include <boost/algorithm/string.hpp>
#include <bits/stl_algo.h>
#include <sstream>

#include <ripple/protocol/tokens.h>

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

//FROM
//https://stackoverflow.com/a/34571089/5155484

// typedef unsigned char uchar;
// static const std::string b = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";//=
// static std::string base64_encode(const std::string &in) {
//     std::string out;

//     int val=0, valb=-6;
//     for (uchar c : in) {
//         val = (val<<8) + c;
//         valb += 8;
//         while (valb>=0) {
//             out.push_back(b[(val>>valb)&0x3F]);
//             valb-=6;
//         }
//     }
//     if (valb>-6) out.push_back(b[((val<<8)>>(valb+8))&0x3F]);
//     while (out.size()%4) out.push_back('=');
//     return out;
// }


namespace gossipClient
{
    GossipMessageClient::GossipMessageClient(std::shared_ptr<Channel> channel, beast::Journal journal)
          : stub_(GossipMessage::NewStub(channel)), journal_(journal)
    {
    }

    int 
    GossipMessageClient::toLibP2P(std::shared_ptr<ripple::Message> const& m, ripple::compression::Compressed compressionEnabled_) 
    {
        // Data we are sending to the server.
        Gossip gossip;
        std::ostringstream pkStream;
        std::string pkSend;

        //RYCB
        //Need to extract the bytes from the message
        //Then transform the bytes in a string (it seems so ugly)

        std::vector<unsigned char> const _buff = m->getBuffer(compressionEnabled_);
        std::string _buffer = bufferToString(_buff);
        // gossip.set_message(_buffer);

        //Set the sender
        auto validator = m->getValidatorKey();
        // std::cout << pthread_self()  << "|" << " full validator key "  << ripple::toBase58(TokenType::NodePublic, m->getValidatorKey);
        if (validator)
        {
            ripple::PublicKey const& validatorKey = *validator;

            std::cout << pthread_self()  << "|" << " full validator key "  << ripple::toBase58(ripple::TokenType::NodePublic, validatorKey) << std::endl;

            // pkStream << validatorKey;
            // pkSend = pkStream.str();

            pkSend.assign(ripple::toBase58(ripple::TokenType::NodePublic, validatorKey));
        }
        else
            pkSend.assign("0");
        
        std::cout << pthread_self()  << "|" << "  validator key sent "  << pkSend << std::endl;

        
        gossip.set_message(_buffer);
        gossip.set_validator_key(pkSend);


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
        if (!status.ok()) 
            return 1;
    }
}