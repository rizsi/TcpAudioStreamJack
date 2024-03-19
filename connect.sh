#!/bin/sh

# Example script to connect a client to a server. 

HOST=myserver.local

# Destroy the existing null-sink pipewire (Pulseaudio) sinks.
pw-cli dump short |grep n=\"null-sink\" |grep " Node " | awk -v FS=: '{print $1}' | xargs pw-cli destroy

# Create a new null-sink. Its monitor port is going to be used to capture audio.
# The user has to select the null-sink (selectable in pavucontrol from the outputs for example) as current output to stream audio to the server.
pactl load-module module-null-sink

# Start the client connecting to the current pipewire server through the Jack API
# The client connects to the $HOST:8080 TCP server and sends the recorded audio
pw-jack jack-tcp-client -u $HOST:8080 -b "null-sink Audio/Sink sink:monitor_"
