# =============================================================================
# Makefile per il modulo kernel kexplorer
# =============================================================================
#
# Variabili principali:
#   obj-m                 : indica al sistema di build del kernel che
#                            kexplorer.o va compilato come modulo (.ko).
#   $(shell uname -r)     : restituisce la versione del kernel in esecuzione,
#                            usata per individuare la cartella degli header.
#   M=$(PWD)              : indica al sistema di build dove si trova il
#                            sorgente del modulo (la cartella corrente).
#
# Bersagli (target):
#   all   : compila il modulo. Genera kexplorer.ko.
#   clean : rimuove tutti i file intermedi prodotti dal build.
#
# Uso tipico:
#   make           # compila
#   sudo insmod kexplorer.ko             # carica
#   sudo insmod kexplorer.ko buffer_size=8192  # carica con buffer custom
#   sudo rmmod  kexplorer                # rimuove
#   make clean     # pulisce
# =============================================================================

obj-m += kexplorer.o

KDIR  := /lib/modules/$(shell uname -r)/build
PWD   := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all clean
