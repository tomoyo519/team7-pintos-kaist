cd threads
make clean
make
cd build
source ../../activate
pintos --gdb -- run priority-donate-one