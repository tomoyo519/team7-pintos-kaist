cd userprog
make clean
make
cd build
source ../../activate
pintos-mkdisk filesys.dsk 10
pintos --gdb --fs-disk filesys.dsk -p tests/userprog/create-normal:create-normal -- -q -f run create-normal