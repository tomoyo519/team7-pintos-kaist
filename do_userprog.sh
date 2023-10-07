cd userprog
make clean
make
cd build
source ../../activate
pintos-mkdisk filesys.dsk 10 
pintos --fs-disk filesys.dsk -p tests/userprog/open-normal:open-normal -- -q -f run open-normal
# pintos -v -k -T 60 -m 20   --fs-disk=10 -p tests/userprog/halt:halt -- -q   -f run exit

