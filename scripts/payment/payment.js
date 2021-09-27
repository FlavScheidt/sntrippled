const RippleAPI = require('ripple-lib').RippleAPI

// TESTNET ADDRESS 1 - SENDER
const ADDRESS_1 = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
const SECRET_1 = "snoPBrXtMeMyMHUVTgbuqAfg1SUTb"

//const ADDRESS_1 = "rLR52VSZG3wqSrkcpfkSnaKnYoYyPoJJgy"
//const SECRET_1 = "ssrnzXKsJKWDh9cFpmZSLWHN3D5HM"

// TESTNET ADDRESS 2 - RECEIVER
const ADDRESS_2 = "rnW7CM4K6FcKbW2NcC8j2TQFXn3FAHdwby"

const instructions = {maxLedgerVersionOffset: 5}
const currency = 'XRP'
const amount = '20000000' //20 million XRP

const payment = {
  source: {
    address: ADDRESS_1,
    maxAmount: {
      value: amount,
      currency: currency
    }
  },
  destination: {
    address: ADDRESS_2,
    amount: {
      value: amount,
      currency: currency
    }
  }
}

const api = new RippleAPI({
  //server: 'wss://s1.ripple.com'                 // MAINNET
  server: 'ws://localhost:6006'
})

api.connect().then(() => {
  console.log('Connected...')
  api.preparePayment(ADDRESS_1, payment, instructions).then(prepared => {
    const {signedTransaction, id} = api.sign(prepared.txJSON, SECRET_1)
    console.log(id)
    api.submit(signedTransaction).then(result => {
      console.log(JSON.stringify(result, null, 2))
      api.disconnect()
    })
  })
}).catch(console.error)