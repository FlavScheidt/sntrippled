/* eslint-disable no-console */
'use strict'

const Libp2p = require('libp2p')
const TCP = require('libp2p-tcp')
const Mplex = require('libp2p-mplex')
const { NOISE } = require('libp2p-noise')
const Gossipsub = require('libp2p-gossipsub')
const uint8ArrayFromString = require('uint8arrays/from-string')
const uint8ArrayToString = require('uint8arrays/to-string')
const MulticastDNS = require('libp2p-mdns')

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



const createNode = async () => {
  const node = await Libp2p.create({
    addresses: {
      listen: ['/ip4/0.0.0.0/tcp/0']
    },
    modules: {
      transport: [TCP],
      streamMuxer: [Mplex],
      connEncryption: [NOISE],
      pubsub: Gossipsub,
      peerDiscovery: [ MulticastDNS ]
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

;(async () => {
  const topic = 'validations'
  const [node1] = await Promise.all([
    createNode()  ])
  console.log("------------------------------------------------------------------")
  console.log("Peer Info: Gossip only")
  console.log("ID:", node1.peerId._idB58String)
  console.log("------------------------------------------------------------------")
  node1.on('peer:discovery', (peer) => console.log('Discovered:', peer.id.toB58String()))

  node1.pubsub.on(topic, (msg) => {
    console.log(Date.now(), " | GossipSub | I received: ", msg.data)
  })

  await node1.pubsub.subscribe(topic)


  // node1 publishes "news" every second
  // setInterval(() => {
  //   node1.pubsub.publish(topic, uint8ArrayFromString(''+node1.peerId._idB58String+', Validation Tx'))
  //   for (let [peerIdString, peer] of node1.peerStore.peers.entries()) {
  //       // peer { id, addresses, metadata, protocols }
  //       console.log(peer.id._idB58String)
  //     }
  // }, 5000)
})()
