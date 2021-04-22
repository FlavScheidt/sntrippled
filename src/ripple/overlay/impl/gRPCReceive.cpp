#include <ripple/overlay/gRPCReceive.h>

//In this case, the rippled acts as the server, receiving the validation and responding with the status

namespace gossipServer
{
    //Does not belong to the object, so we are able to create the pthread
    void * 
    Run(void * ret)
    {
        std::cout << "THREAD NUMBER " << gettid() << std::endl;
        gossipServer::GossipMessageImpl *grpcIn;
        grpcIn = new GossipMessageImpl();

        grpcIn->ConnectAndRun();
    }

    GossipMessageImpl::~GossipMessageImpl() 
    {

        server_->Shutdown();
        // Always shutdown the completion queue after the server.
        cq_->Shutdown();

        return;
    }

    // There is no shutdown handling in this code.

    Status 
    GossipMessageImpl::toRippled(ServerContext* context, const Gossip* gossip, Control* control) 
    {
        std::cout << "Message received:" << std::endl;
        std::cout << gossip->message() << std::endl;

        control->set_stream(1);

        return Status::OK;
    }


    void
    GossipMessageImpl::ConnectAndRun()
    {
        std::string server_address("0.0.0.0:20052");
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
        std::cout << "gRPC Server listening on " << server_address << std::endl;
        // Proceed to the server's main loop.
        HandleRpcs();

        std::cout << "bye" << std::endl;

        pthread_exit(0);
    }

    // Take in the "service" instance (in this case representing an asynchronous
    // server) and the completion queue "cq" used for asynchronous communication
    // with the gRPC runtime.
    GossipMessageImpl::CallData::CallData(GossipMessage::AsyncService* service, ServerCompletionQueue* cq)
        : service_(service), cq_(cq), responder_(&ctx_), status_(CREATE) 
    {
        // Invoke the serving logic right away.
        GossipMessageImpl::CallData::Proceed();
    }

    void 
    GossipMessageImpl::CallData::Proceed() 
    {
        std::cout << "enter proceed" << std::endl;
        if (status_ == CREATE) 
        {
            std::cout << "create" << std::endl;
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
            std::cout << "process" << std::endl;
            // Spawn a new CallData instance to serve new clients while we process
            // the one for this CallData. The instance will deallocate itself as
            // part of its FINISH state.
            // new CallData(service_, cq_);
            // // The actual processing.
            std::string prefix("Hello ");
            std::cout << "gRPC message received\n"; //the message processing goes here
            control.set_stream(true);
            // And we are done! Let the gRPC runtime know we've finished, using the
            // memory address of this instance as the uniquely identifying tag for
            // the event.
            status_ = FINISH;
            responder_.Finish(control, Status::OK, this);
        } 
        else 
        {
            std::cout << "assert" << std::endl;
            GPR_ASSERT(status_ == FINISH);
            // Once in the FINISH state, deallocate ourselves (CallData).
            delete this;
        }
        std::cout << "leaving" << std::endl;
    }


    // This can be run in multiple threads if needed.
    void 
    GossipMessageImpl::HandleRpcs() 
    {
        std::cout << "enter handle" << std::endl;
        // Spawn a new CallData instance to serve new clients.
        new GossipMessageImpl::CallData(&service_, cq_.get());
        void* tag;  // uniquely identifies a request.
        bool ok;
        while (true) 
        {
            std::cout << "true" << std::endl;
            // Block waiting to read the next event from the completion queue. The
            // event is uniquely identified by its tag, which in this case is the
            // memory address of a CallData instance.
            // The return value of Next should always be checked. This return value
            // tells us whether there is any kind of event or cq_ is shutting down.
            GPR_ASSERT(cq_->Next(&tag, &ok));
            GPR_ASSERT(ok);
            static_cast<GossipMessageImpl::CallData*>(tag)->GossipMessageImpl::CallData::Proceed();
        }
        std::cout << "leave handle" << std::endl;
    }

};
