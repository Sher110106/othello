# Makefile for MyBot.so
# Compiles the Othello bot into a shared library

ROOTDIR  = ../../
include $(ROOTDIR)Makefile.inc

BOT = MyBot

all: $(BOT).so

$(BOT).so: $(BOT).cpp $(ROOTDIR)lib/libOthello.so
	$(CC) $(LDFLAGS) $(CFLAGS) -shared $^ -o $@

.PHONY: clean 

clean:
	rm -rf $(BOT).so $(BOT).o

# Rebuild from scratch
rebuild: clean all

# Install (optional - copy to common location)
install: $(BOT).so
	@echo "To test your bot, run:"
	@echo "./bin/Desdemona ./bots/MyBot/$(BOT).so ./bots/RandomBot/RandomBot.so"

.PHONY: all clean rebuild install
