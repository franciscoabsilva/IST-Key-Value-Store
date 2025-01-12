# Run make
make clean
make

cd src
cd server
clear
valgrind ./kvs jobs 1 1 baba
