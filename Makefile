cleaner: cleaner.o
	$(CC) $(CFLAGS) -o cleaner cleaner.o -lreadline
cleaner.o: cleaner.c
	$(CC) $(CFLAGS) -c cleaner.c
clean:
	-rm *.o
	-rm cleaner
