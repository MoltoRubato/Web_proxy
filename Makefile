EXE=htproxy
OBJS=htproxy.o socket.o extract.o cache.o

$(EXE): $(OBJS)
	cc -Wall -o $@ $(OBJS)

htproxy.o: htproxy.c htproxy.h
	cc -Wall -c htproxy.c

socket.o: socket.c htproxy.h
	cc -Wall -c socket.c

extract.o: extract.c htproxy.h
	cc -Wall -c extract.c

cache.o: cache.c cache.h
	cc -Wall -c cache.c

format:
	clang-format -style=file -i *.c

clean:
	rm -f $(EXE) *.o *.txt