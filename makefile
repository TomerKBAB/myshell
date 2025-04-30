all: myshell looper myPipe mypipeline

myPipe: myPipe.c
	gcc -Wall -g -o myPipe myPipe.c

looper: looper.c
	gcc -Wall -g -o looper looper.c

myshell: LineParser.o myshell.o
	gcc -Wall -g -o myshell LineParser.o myshell.o

myshell.o: myshell.c
	gcc -Wall -g -c myshell.c

LineParser.o: LineParser.c LineParser.h
	gcc -Wall -g -c LineParser.c

mypipeline: mypipeline.c
	gcc -Wall -g -o mypipeline mypipeline.c

clean:
	rm -r myshell.o LineParser.o myshell looper myPipe mypipeline