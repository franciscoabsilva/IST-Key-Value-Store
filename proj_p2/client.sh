cd src
cd client
clear
valgrind --leak-check=full  ./client "$1" "/home/franciscosilva/Area de Trabalho/SO/IST-Key-Value-Store/proj_p2/src/server/baba"

