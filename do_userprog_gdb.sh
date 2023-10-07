cd userprog
make clean
make
cd build
source ../../activate
pintos-mkdisk filesys.dsk 10
pintos --gdb --fs-disk filesys.dsk -p tests/userprog/create-normal:create-normal -- -q -f run create-normal
# intos --gdb --fs-disk=10 -p tests/userprog/args-single:args-single -- -q -f run 'args-single'