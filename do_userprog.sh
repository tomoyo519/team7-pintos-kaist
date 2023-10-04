cd userprog
make clean
make
cd build
source ../../activate
pintos --fs-disk filesys.dsk -p tests/userprog/args-single:args-single -- -q -f run 'args-single onearg'