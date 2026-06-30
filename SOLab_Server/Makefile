obj := solabd solabc gen_db
all: $(obj)
%: %.c solab.h
	gcc -Wall -O2 -o $@ $< -lpthread
clean:
	rm -f $(obj)
