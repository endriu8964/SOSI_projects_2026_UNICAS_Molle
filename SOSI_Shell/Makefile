# ************************************************************************** #
#  Makefile - SOSI Shell
#  Corso di Sistemi Operativi e Sicurezza Informatica
#
#  Target principali:
#    make            compila la shell (target di default: sosi_shell)
#    make run        compila ed esegue la shell
#    make debug      compila con simboli di debug e sanitizer
#    make clean      rimuove l'eseguibile e i file oggetto
# ************************************************************************** #

CC      = gcc
CFLAGS  = -Wall -Wextra -g
TARGET  = sosi_shell
SRC     = SOSI_shell.c

# Prima regola = radice dell'albero delle dipendenze (target di default)
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

run: $(TARGET)
	./$(TARGET)

# Build con AddressSanitizer per individuare errori di memoria a runtime
debug: $(SRC)
	$(CC) $(CFLAGS) -fsanitize=address -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) *.o

.PHONY: run debug clean
