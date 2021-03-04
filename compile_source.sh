#!/bin/sh
apt-get update
apt-get install -y gcc g++ wget git cmake pkg-config protobuf-compiler libprotobuf-dev libssl-dev
wget https://dl.bintray.com/boostorg/release/1.70.0/source/boost_1_70_0.tar.gz
tar -xzf boost_1_70_0.tar.gz
cd boost_1_70_0
./bootstrap.sh
./b2 headers
./b2 -j 1
cd ..
git clone https://github.com/ripple/rippled.git
cd rippled
git checkout release
export BOOST_ROOT=~/boost_1_70_0
mkdir my_build
cd my_build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/ripple -Dassert=ON ..
cmake --build . --target install -- -j 1
