sudo apt-get update
sudo apt-get -y upgrade
sudo apt-get -y install git pkg-config protobuf-compiler libprotobuf-dev libssl-dev wget build-essential

wget https://github.com/Kitware/CMake/releases/download/v3.13.3/cmake-3.13.3-Linux-x86_64.sh
sudo sh cmake-3.13.3-Linux-x86_64.sh --prefix=/usr/local --exclude-subdir

cmake --version 

wget https://dl.bintray.com/boostorg/release/1.71.0/source/boost_1_71_0.tar.gz
tar xvzf boost_1_71_0.tar.gz

cd boost_1_71_0
./b2 -j 4
export BOOST_ROOT=/home/my_user/boost_1_71_0

source ~/.profile

git clone https://github.com/FlavScheidt/rippled.git
cd rippled
git checkout develop

cd ..
mkdir my_build
cd my_build
cmake ..
cmake --build .
./rippled -u
