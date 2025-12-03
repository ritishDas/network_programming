CC = gcc -std=c17
FLAGS = -O2 -Werror -Wall -Wextra
BUILD_DIR = build
SRC_DIR = util
HEADER_DIR = header

all: $(BUILD_DIR)/app
	./$^

book: $(BUILD_DIR)/book
	./$^

server: $(BUILD_DIR)/server
	./$^


$(BUILD_DIR)/app: app.c 
	$(CC) $(FLAGS) $^ -o $@


$(BUILD_DIR)/rdnetwork.o: $(SRC_DIR)/rdnetwork.c $(HEADER_DIR)/rdnetwork.h
	$(CC) $(FLAGS) -I$(HEADER_DIR) -c $(SRC_DIR)/rdnetwork.c -o $(BUILD_DIR)/rdnetwork.o


$(BUILD_DIR)/server: server.c
	$(CC) $(FLAGS) $^ -o $@

$(BUILD_DIR)/book: book.c
	$(CC) $(FLAGS) $^ -o $@

