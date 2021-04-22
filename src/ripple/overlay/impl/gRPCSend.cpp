#include <ripple/overlay/gRPCSend.h>

#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/insert_linebreaks.hpp>
#include <boost/archive/iterators/remove_whitespace.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/archive/iterators/ostream_iterator.hpp>
#include <boost/algorithm/string.hpp>
#include <bits/stl_algo.h>

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

typedef unsigned char uchar;
static const std::string b = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";//=
static std::string base64_encode(const std::string &in) {
    std::string out;

    int val=0, valb=-6;
    for (uchar c : in) {
        val = (val<<8) + c;
        valb += 8;
        while (valb>=0) {
            out.push_back(b[(val>>valb)&0x3F]);
            valb-=6;
        }
    }
    if (valb>-6) out.push_back(b[((val<<8)>>(valb+8))&0x3F]);
    while (out.size()%4) out.push_back('=');
    return out;
}


// static std::string base64_decode(const std::string &input) {

//     std::string out;

//     static constexpr unsigned char kDecodingTable[] = {
//       64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
//       64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
//       64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
//       52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
//       64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
//       15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
//       64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
  //     41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
  //     64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  //     64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  //     64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  //     64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  //     64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  //     64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  //     64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  //     64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
  //   };

  //   size_t in_len = input.size();
  //   if (in_len % 4 != 0) return "Input data size is not a multiple of 4";

  //   size_t out_len = in_len / 4 * 3;
  //   if (input[in_len - 1] == '=') out_len--;
  //   if (input[in_len - 2] == '=') out_len--;

  //   out.resize(out_len);

  //   for (size_t i = 0, j = 0; i < in_len;) {
  //     uint32_t a = input[i] == '=' ? 0 & i++ : kDecodingTable[static_cast<int>(input[i++])];
  //     uint32_t b = input[i] == '=' ? 0 & i++ : kDecodingTable[static_cast<int>(input[i++])];
  //     uint32_t c = input[i] == '=' ? 0 & i++ : kDecodingTable[static_cast<int>(input[i++])];
  //     uint32_t d = input[i] == '=' ? 0 & i++ : kDecodingTable[static_cast<int>(input[i++])];

  //     uint32_t triple = (a << 3 * 6) + (b << 2 * 6) + (c << 1 * 6) + (d << 0 * 6);

  //     if (j < out_len) out[j++] = (triple >> 2 * 8) & 0xFF;
  //     if (j < out_len) out[j++] = (triple >> 1 * 8) & 0xFF;
  //     if (j < out_len) out[j++] = (triple >> 0 * 8) & 0xFF;
  //   }

  //   return out;
  // }


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
        // std::string ipsum = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Maecenas tincidunt egestas blandit. Donec pretium commodo dui, sed condimentum erat ultricies vitae. Morbi non feugiat enim, et lobortis nunc. Pellentesque habitant morbi tristique senectus et netus et malesuada fames ac turpis egestas. Ut sem massa, vulputate ac ligula nec, porta venenatis quam. Donec consequat sagittis leo, quis lacinia magna fringilla at. Phasellus mattis lorem nec lacus commodo elementum vitae et orci. Etiam consectetur, purus in iaculis consectetur, ipsum velit sodales libero, at lobortis justo est quis purus. Aliquam id pulvinar justo, eget laoreet elit. Quisque purus leo, imperdiet vel dapibus quis, suscipit vitae ipsum. Sed congue nulla sed risus consequat cursus. Ut a congue erat, in dignissim enim. Sed condimentum dapibus felis, a molestie dolor maximus eu. Maecenas eleifend id lacus mattis imperdiet. Nulla tempor, tellus a dictum pellentesque, tortor turpis aliquam elit, vitae euismod nisi urna ut tortor. ";
        //RYCB
        //Need to extract the bytes from the message
        //Then transform the bytes in a string (it seems so ugly)
        std::vector<unsigned char> const _buff = m->getBuffer(compressionEnabled_);
        std::string _buffer = base64_encode(bufferToString(_buff));
        // std::cout << "___________________________________________" << std::endl;
        // std::cout << _buffer << std::endl;
        // std::cout << "___________________________________________" << std::endl;
        gossip.set_message(_buffer);
        // gossip.set_message(ipsum);


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
            // std::cout << "Validation Sent via gRPC succesfully\n";
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