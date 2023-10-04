cd userprog
make clean
make
cd build
source ../../activate
pintos --gdb --fs-disk filesys.dsk -p tests/userprog/args-single:args-single -- -q -f run 'args-single onearg'