.PHONY: clean

ddup: 
	gcc -std=c99 -O -Wall -Wextra -pthread `xml2-config --cflags` ddupserver.c -lssl -lcrypto `xml2-config --libs` -o ddupserver.out
	gcc -std=c99 -O -Wall -Wextra -pthread `xml2-config --cflags` ddupclient.c -lssl -lcrypto `xml2-config --libs` -o ddupclient.out

xml:
	gcc -o xml.out -std=c99 `xml2-config --cflags` xmlLogic.c `xml2-config --libs`

clean:
	$(RM) *.out