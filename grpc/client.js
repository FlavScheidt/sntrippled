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
//Edited by Wazen 11/6
//gossip section
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

fs = require('fs');
const myKey = fs.readFileSync('key.out', 'utf8').split('\n')[0];
console.log(myKey)
function hexToBase58(key) {
  const payload = Buffer.from("1C" + key, 'hex');
  const checksum = Buffer.from(sha256.x2(payload), 'hex').slice(0,4);
  return bs58.encode(Buffer.concat([payload, checksum]));
}

var PROTO_PATH = __dirname + '/gossip_message.proto';
var parseArgs = require('minimist');
var grpc = require('@grpc/grpc-js');
var protoLoader = require('@grpc/proto-loader');
var packageDefinition = protoLoader.loadSync(
    PROTO_PATH,
    {keepCase: true,
     longs: String,
     enums: String,
     defaults: true,
     oneofs: true
    });
var gossip_proto = grpc.loadPackageDefinition(packageDefinition).gossipmessage;

////////////////////////////////////////////////////////////////////////////////////
// Gossip section 
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
//////////////////////////////////////////////////////////////


async function main() {
 // Gossip section 
 const topic = 'validations'
 const [node1] = await Promise.all([ createNode()  ])
console.log("------------------------------------------------------------------")
console.log("Peer Info: Gossib function")
console.log("ID:", node1.peerId._idB58String)
console.log("------------------------------------------------------------------")

  await node1.pubsub.subscribe(topic)
 
  /////////////////////////////////////////////////////
  // console.log('enter main');
  var argv = parseArgs(process.argv.slice(2), {
    string: 'target'
  });
  var target;
  if (argv.target) {
    target = argv.target;
  } else {
    target = 'localhost:20052';
  }
  var client = new gossip_proto.GossipMessage(target,
                                       grpc.credentials.createInsecure());
  console.log(Date.now(), ' | gRPC | Chanel created');
  //var mess;
  //if (argv._.length > 0) {
  //  mess = argv._[0]; 
  //} else {
  //  mess = 'AAAA6wApCugBIoAAAAEmAAqu4CkoFJRnOhhzMPeiQk/QUdpy95wgfe72yP3/k/JZ18XqhmOLa7QJ7+YULlNwNCVqUBcAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAFAZL8sR3hL5qqcpwMNbqkiUGkM+vRjkojtbP7SgiBvxt5xzIQK7divurbJKMYI6AGpyGnZKSt+ty3Pv+TIzb2Bai/OEeHZGMEQCIAw2dE1DjbWleyeJQ0DlhygRLgBehhhcRe75B3yGJADsAiBoH1TZ19hMEfBXwTR6DQkVvjx2vt/6i18IotHum8RSTQ==';
  //}
  //console.log('message set');
 
  node1.pubsub.on(topic, (msg) => {
  //let buff = new Buffer(msg.data);
  //let base64data = buff.toString('base64');
   //console.log("-------------------------")
console.log(msg.data.toString())
   try
    {
    	msg2send = JSON.parse(msg.data)
        console.log('I received: ', msg2send.msg)
        if(msg2send.validator_key != myKey)
        {
   		client.toRippled("Hello", function(err, response) {
    	   		console.log(Date.now(), ' | gRPC | Message sent to rippled server');
    		});
        }
   }
   catch(e)
	{
	//console.log(e)
	}
  })

 // client.toRippled({message: mess}, function(err, response) {
 //   console.log('Message sent');
 // });
}

main();
