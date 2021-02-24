cleaner: cleaner.o
	clang -o cleaner cleaner.o -lreadline
cleaner.o: cleaner.c
	clang -c cleaner.c
