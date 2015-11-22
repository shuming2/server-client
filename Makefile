procnanny : procnanny.server.c procnanny.client.c memwatch.c
	gcc -Wall -DMEMWATCH -DMW_STDIO procnanny.server.c memwatch.c -o procnanny.server
	gcc -Wall -DMEMWATCH -DMW_STDIO procnanny.client.c memwatch.c -o procnanny.client

clean:
	rm -f *.o procnanny.server
	rm -f *.o procnanny.client