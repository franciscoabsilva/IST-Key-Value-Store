# Run make
make clean
make

cd src
cd server
clear
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --keep-debuginfo=yes ./kvs jobs 1 1 baba
