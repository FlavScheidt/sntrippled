#include <ripple/overlay/gRPCSend.h>
#include <ripple/overlay/impl/sha512.hh>

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


//Execute command to get the ephemeral key and put it into the proto messag
//from https://stackoverflow.com/a/478960
std::string execShell(const char* cmd)
{
    std::array<char, 128> buffer;
    std::string result;

    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
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
//             //If the message is not originated here, we shouldnt send it to grpc

//             ripple::PublicKey const& validatorKey = *validator;
//             // pkStream << validatorKey;
//             // pkSend = pkStream.str();

//             pkSend.assign(ripple::toBase58(ripple::TokenType::NodePublic, validatorKey));
              return 2;
        }
        else
        {
            auto ephemeralKey = execShell("cat key.out");
            pkSend.assign(ephemeralKey);
            // std::cout << "Got node's own key" << std::endl;


            // std::string _test = "Hello World =)";
            gossip.set_message(_buffer);
            // gossip.set_message(_test);
            gossip.set_validator_key(pkSend);

            // auto messHash = ripple::sha512Half(ripple::makeSlice(m->getBuffer(compressionEnabled)));
            // messHash.erase(std::remove(messHash.begin(), messHash.end(), '\n'), messHash.end());

            // SHA512 sha512; //instantiate a SHA512 object
            // sha512.hash(_buffer); //returns the hash as a string
            std::string messageHash = sw::sha512::calculate(&_buffer, sizeof(_buffer));
            messageHash.erase(std::remove(messageHash.begin(), messageHash.end(), '\n'), messageHash.end());
        
            std::cout << pthread_self() << "| message sent |" << pkSend << " | " << messageHash << std::endl;


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
          
            reuturn 0;
        }

    }
}

