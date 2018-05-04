# Audiovisualizer #

A audio visualizer using GTK and PulseAudio

compile:
gcc -pthread -lm -o <NAME> audiovisualizer.c `pkg-config --cflags --libs libpulse libpulse-simple gtk+-3.0`
