STACK_BASE	:= $(HOME)/.stack/programs/x86_64-linux
VERSION		:= 8.10.4

INCLUDE		:= $(STACK_BASE)/ghc-$(VERSION)/lib/ghc-$(VERSION)/include

%.o:		%.c trav.h
			gcc -std=c11 -pthread -O2 -fpic \
				-Wall -Wextra -Wconversion -Wsign-conversion \
				-I$(INCLUDE) -c -o $@ $<

dump.so:	dump.o trav.o
			gcc -pthread -shared -Wl,--no-undefined -o dump.so dump.o trav.o

clean:
			rm -f dump.so *.o
