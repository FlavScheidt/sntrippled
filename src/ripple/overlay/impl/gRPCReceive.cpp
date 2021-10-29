#include <ripple/overlay/gRPCReceive.h>

#include <sstream>
#include <iostream>

#include <boost/property_tree/json_parser.hpp>
#include <boost/json.hpp>
#include <boost/json/src.hpp>

#include <boost/algorithm/clamp.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/beast/core/ostream.hpp>
#include "boost/date_time/posix_time/posix_time.hpp"

//In this case, the rippled acts as the server, receiving the validation and responding with the status

//RYCB Beat me up, I'm doing ugly stuff
int gRPCportNum = 1;

namespace gossipServer
{
    //Does not belong to the object, so we are able to create the pthread
    //Need to pass a pointer to the peerimp object to be able to handle the messages

    //Alter here to freceive the log app_.journal(gRPCServer)
    void * 
    Run(void * tArguments)
    {
        gossipServer::runArguments *args = static_cast<gossipServer::runArguments *>(tArguments);
        
        beast::Journal journal = static_cast<beast::Journal>(args->journal);
        // ripple::PeerImp *peerObject = static_cast<ripple::PeerImp *>(overlay);
        gossipServer::GossipMessageImpl *grpcIn;

        pthread_mutex_lock(&gRPClock);
        grpcIn = new GossipMessageImpl(journal);

        if (gRPCportNum == 2)
        {
            pthread_mutex_unlock(&gRPClock);
            JLOG(journal.debug()) << "Thread number " << pthread_self() <<  " initiating gRPC server";
            grpcIn->ConnectAndRun(args->overlay);
        }
        else
        {
            pthread_mutex_unlock(&gRPClock);
            JLOG(journal.debug()) << "Thread number " << pthread_self() <<  " trying to initiate new gRPC server. Server won't be started for safety. Destroying object alocated";
            // delete grpcIn;
        }

    }


    void dump_buffer(std::ostream& os, multi_buffer const& mb) 
    {
        os << mb.size() << " (" << mb.capacity() << ") "
           << "'" << boost::beast::buffers(mb.data()) << "'\n";
    }

    GossipMessageImpl::GossipMessageImpl(beast::Journal journal) : journal_(journal)
    {
        //RYCB ugly logics for the port number
        //Cause there are threads here and they made my day harder
        // journal_ = journal;
        std::string portNumber;

        gRPCportNum++;
        int gRPCportNumAux = gRPCportNum;

        portNumber = boost::lexical_cast<std::string>(gRPCportNumAux);

        gRPCport = "0.0.0.0:2005" + portNumber;

        JLOG(journal_.debug()) << "gRPC server object created succesfully";

    }

    GossipMessageImpl::~GossipMessageImpl() 
    {

        server_->Shutdown();
        // Always shutdown the completion queue after the server.
        cq_->Shutdown();

        return;
    }

    // There is no shutdown handling in this code.

    // Status 
    // GossipMessageImpl::toRippled(ServerContext* context, const Gossip* gossip, Control* control) 
    // {


    // }


    void
    GossipMessageImpl::ConnectAndRun(void *  overlay)
    {
        std::string server_address(gRPCport);
        ServerBuilder builder;
        // Listen on the given address without any authentication mechanism.
        builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
        // Register "service_" as the instance through which we'll communicate with
        // clients. In this case it corresponds to an *asynchronous* service.
        builder.RegisterService(&service_);
        // Get hold of the completion queue used for the asynchronous communication
        // with the gRPC runtime.
        cq_ = builder.AddCompletionQueue();
        // Finally assemble the server.
        server_ = builder.BuildAndStart();
        JLOG(journal_.debug()) << "gRPC server listening on port " << gRPCport;
        // Proceed to the server's main loop.
        HandleRpcs(overlay);

        pthread_exit(0);
    }

    // Take in the "service" instance (in this case representing an asynchronous
    // server) and the completion queue "cq" used for asynchronous communication
    // with the gRPC runtime.
    GossipMessageImpl::CallData::CallData(GossipMessage::AsyncService* service, ServerCompletionQueue* cq, void *  overlay, beast::Journal journal)
        : service_(service), cq_(cq), responder_(&ctx_), status_(CREATE), journal_(journal)
    {
        // Invoke the serving logic right away.
        GossipMessageImpl::CallData::Proceed(overlay);
    }


    std::wstring 
    FormatTime(boost::posix_time::ptime now)
    {
      using namespace boost::posix_time;
      static std::locale loc(std::wcout.getloc(),
                             new wtime_facet(L"%Y%m%d_%H%M%S"));

      std::basic_stringstream<wchar_t> wss;
      wss.imbue(loc);
      wss << now;
      return wss.str();
    }

    void 
    GossipMessageImpl::CallData::Proceed(void *  overlay) 
    {
        using namespace boost::posix_time;
        std::size_t bytes_transferred;
        boost::system::error_code ec;
        std::size_t bytes_consumed;


        // std::cout << "Call Data" << std::endl;

        if (status_ == CREATE) 
        {
            // Make this instance progress to the PROCESS state.
            status_ = PROCESS;
            // As part of the initial CREATE state, we *request* that the system
            // start processing SayHello requests. In this request, "this" acts are
            // the tag uniquely identifying the request (so that different CallData
            // instances can serve different requests concurrently), in this case
            // the memory address of this CallData instance.
            service_->RequesttoRippled(&ctx_, &gossip, &responder_, cq_, cq_,
                                      this);
        } 
        else if (status_ == PROCESS) 
        {
            // std::cout << "process" << std::endl;
            // Spawn a new CallData instance to serve new clients while we process
            // the one for this CallData. The instance will deallocate itself as
            // part of its FINISH state.
            new CallData(service_, cq_, overlay, journal_);
            // // The actual processing.
 
            //RYCB
            //Invocking the protocol to treat the message
            // Since we are not usting beast to receive the message, we don't nedd
            // to call read_some. So I will just copy what's on gossip.message()
            // inside the buffer, make the same treatment we have on onReadMessage
            // and invoke invokeProtocolMessage(). Then I just need to pray.

            //The tricky part is that the message is sent as a json
            // so node.js won't corrupt the data with the string enconding
            // Which means that we need to extract the message from the json

            // Side note: node.js makes me drink
            // Reading as a json
            namespace json = boost::json;
            auto doc = json::parse(gossip.message()).as_object();

            auto& arr   = doc["message"].as_object()["data"];
            auto  bytes = json::value_to<std::vector<uint8_t>>(arr);
            std::string text(bytes.begin(), bytes.end());

            std::stringstream ss;
            ss << std::quoted(text);
            std::string message_received = ss.str();

            // std::cout << "Message received pure: " << gossip.message() << std::endl;
            // std::cout << "Message received, json: " << message_received << std::endl;
            // dump_buffer(std::cout << "Message received json : ", message_received);            

            //Here is the copy
            bytes_transferred = boost::asio::buffer_copy(read_buffer_grpc.prepare(bytes.size()), boost::asio::buffer(bytes));
            read_buffer_grpc.commit(bytes_transferred);

            ptime now = second_clock::universal_time();

            std::wstring ws(FormatTime(now));
            // std::wcout << ws << std::endl;

            std::string bufferToPrint = boost::beast::buffers_to_string(read_buffer_grpc.data());
            bufferToPrint.erase(std::remove(bufferToPrint.begin(), bufferToPrint.end(), '\n'), bufferToPrint.end());

            //Prepare the buffer to be read

            read_buffer_grpc.commit(bytes_transferred);

            //Hin is zero just because today is tuesday
            //peerObject is the handler
            std::size_t  hint = 0;

            //Get peerOjbect from the translation table on the overlay object
            ripple::OverlayImpl *ovl = static_cast<ripple::OverlayImpl *>(overlay);

            //Get the validator key from the message itself
            auto validator_key = static_cast<std::string>(gossip.validator_key());

            std::shared_ptr<ripple::PeerImp> peerObject = ovl->peerObjs[gossip.validator_key()];
            auto peerID_rcv = peerObject->id();
            std::cout << "RYCB Peer selected: " << peerID_rcv << std::endl;

            std::wcout << ws;
            std::cout << " | Message received | \"" << bufferToPrint << "\" | \"" << validator_key << " \"" << std::endl;

            //Read and process buffer, unless there is an error on invokeProtoclMessage
            while (read_buffer_grpc.size() > 0)
            {
                std::tie(bytes_consumed, ec) =
                    ripple::invokeProtocolMessage(read_buffer_grpc.data(), *peerObject, hint);
                if (ec)
                {
                    JLOG(journal_.warn()) << "ERROR on receiving gRPC message " << ec.message();
                }
                if (bytes_consumed == 0)
                    break;
                read_buffer_grpc.consume(bytes_consumed);
            }

            //On the original function, tries to read from the socket
            //However, we are merely emulating this behavior, when in 
            // fact we receive the message via gRPC and not HTTP
            // which means that we already have a loop occuring
            // and we don't need to trigger onReadMessage using
            // bind() to call the funtion recursivelly as a handler

            control.set_stream(1); //set the response

            // And we are done! Let the gRPC runtime know we've finished, using the
            // memory address of this instance as the uniquely identifying tag for
            // the event.
            status_ = FINISH;
            responder_.Finish(control, Status::OK, this);
        } 
        else 
        {
            GPR_ASSERT(status_ == FINISH);
            // Once in the FINISH state, deallocate ourselves (CallData).
            delete this;
        }
    }


    // This can be run in multiple threads if needed.
    void 
    GossipMessageImpl::HandleRpcs(void *  overlay) 
    {
        // Spawn a new CallData instance to serve new clients.
        new GossipMessageImpl::CallData(&service_, cq_.get(), overlay, journal_);
        void* tag;  // uniquely identifies a request.
        bool ok;
        while (true) 
        {
            // Block waiting to read the next event from the completion queue. The
            // event is uniquely identified by its tag, which in this case is the
            // memory address of a CallData instance.
            // The return value of Next should always be checked. This return value
            // tells us whether there is any kind of event or cq_ is shutting down.
            GPR_ASSERT(cq_->Next(&tag, &ok));
            GPR_ASSERT(ok);
            static_cast<GossipMessageImpl::CallData*>(tag)->GossipMessageImpl::CallData::Proceed(overlay);
        }
    }

};
