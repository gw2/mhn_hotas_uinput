all: hori

hori: hori.c
	$(CC) -Wall -o hori hori.c -lusb-1.0 -Ofast

clean:
	rm hori
