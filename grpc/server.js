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


// Gossibsub part

const Libp2p = require('libp2p')
const TCP = require('libp2p-tcp')
const Mplex = require('libp2p-mplex')
const { NOISE } = require('libp2p-noise')
const Gossipsub = require('libp2p-gossipsub')
const uint8ArrayFromString = require('uint8arrays/from-string')
const uint8ArrayToString = require('uint8arrays/to-string')
const MulticastDNS = require('libp2p-mdns')
const bs58 = require('ripple-bs58');
const base64 = require('base-64');
const sha256 = require('sha256');


function hexToBase58(key) {
  const payload = Buffer.from("1C" + key, 'hex');
  const checksum = Buffer.from(sha256.x2(payload), 'hex').slice(0,4);
  return bs58.encode(Buffer.concat([payload, checksum]));
}

const createNode = async() => {
    const node = await Libp2p.create({
        addresses: {
            listen: ['/ip4/0.0.0.0/tcp/0']
        },
        modules: {
            transport: [TCP],
            streamMuxer: [Mplex],
            connEncryption: [NOISE],
            pubsub: Gossipsub,
            peerDiscovery: [MulticastDNS]
        },
        config: {
            peerDiscovery: {
                mdns: {
                    interval: 20e3,
                    enabled: true
                }
            }
        }
    })

    await node.start()

    return node
}

let my_node;
const topic = 'validations'

const gosssib = async() => {

    const [node1] = await Promise.all([
        createNode()
    ])
    my_node = node1
    console.log("------------------------------------------------------------------")
    console.log("Peer Info: Gossib + toLibP2P gRPC function")
    console.log("ID:", node1.peerId._idB58String)
    console.log("------------------------------------------------------------------")
    node1.on('peer:discovery', (peer) => console.log(Date.now(), " | Discovered:", peer.id.toB58String()))

    node1.pubsub.on(topic, (msg) => {
        // console.log(`${uint8ArrayToString(msg.data)}`)
        //console.log(Date.now(), " | GossipSub | I received: ", msg.data)
    })

    await node1.pubsub.subscribe(topic)
}


var PROTO_PATH = __dirname + '/gossip_message.proto';
var grpc = require('@grpc/grpc-js');
var protoLoader = require('@grpc/proto-loader');
var packageDefinition = protoLoader.loadSync(
    PROTO_PATH, {
        keepCase: true,
        longs: String,
        enums: String,
        defaults: true,
        oneofs: true
    });
var gossip_message = grpc.loadPackageDefinition(packageDefinition).gossipmessage;

/**
 * Implements the toLibP2P RPC method.
 */
function toLibP2P(call, callback) {
    //my_node.pubsub.publish(topic, uint8ArrayFromString('' + call.request.name + ', Validation Tx'))

    let buff = new Buffer(call.request.message);
    let base64data = buff.toString("base64");
    console.log("___________________________________________")
    console.log(Date.now(), " | gRPC | I received Msg: ", call.request.message.toString())
    console.log(Date.now(), " | gRPC | Msg Validation key(native): ", call.request.validator_key.toString())
    //console.log(Date.now(), " | gRPC | Msg Validation key(bs58): ", hexToBase58(call.request.validator_key))
    
    // Wazen: gossibsub publish
       //if(call.request.validator_key!="n94pUb4paSJo8pMimntNPYMyeyqrXwsMDcyArSgDcmLcGRwRq2D8")
	//{ //my_node.pubsub.publish(topic, call.request.message)
    msg_to_brodcast =JSON.stringify({msg:call.request.message.toString(), validator_key:call.request.validator_key.toString()})
    my_node.pubsub.publish(topic, msg_to_brodcast) //publish the whole msg + validator key
	//}
    console.log("___________________________________________")
    callback(null, {
        message: 'True'
    });
}

/**
 * Starts a RPC server that receives requests for the toLibP2P service at the
 * sample server port
 */
async function main() {
    var server = new grpc.Server();
    server.addService(gossip_message.GossipMessage.service, { toLibP2P: toLibP2P });
    server.bindAsync('0.0.0.0:50051', grpc.ServerCredentials.createInsecure(), () => {
        server.start();
    });
    gosssib();
}

main();
