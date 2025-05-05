CC = gcc
CFLAGS = -Wall -Wextra -std=c17

SOURCES = traceroute.c
OBJECTS = $(SOURCES:.c=.o)
EXECUTABLE = traceroute

make: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean: 
	rm -f $(OBJECTS)