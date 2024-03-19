
# Compile:
# gcc capture_client.c  -ljack -lsndfile

#  List with aliases
# $ pw-jack jack_lsp -A

# Connect ports: pw-jack qjackctl


all: jack-tcp-server jack-tcp-client

.PHONY: all clean

jack-tcp-server: jack-tcp-server.c linked_list.c ringBuffer.c
	gcc -g -o jack-tcp-server jack-tcp-server.c linked_list.c ringBuffer.c -ljack -lspeexdsp

jack-tcp-client: jack-tcp-client.c ringBuffer.c
	gcc -g -o jack-tcp-client jack-tcp-client.c ringBuffer.c -ljack

clean:
	rm -f jack-tcp-server jack-tcp-client

install: all
	cp jack-tcp-server ~/.local/bin/
	cp jack-tcp-client ~/.local/bin/
