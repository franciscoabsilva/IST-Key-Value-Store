# Run make
make clean
make

cd src
cd server
clear
valgrind --leak-check=full ./kvs jobs 1 1 baba
