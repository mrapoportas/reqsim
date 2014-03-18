
.PHONY: clean

# $^ is seemingly unavailable in MINIX (3.2.1).
reqsim: builtinjobs.o reqsim.o
	@$(CC) -o $@ builtinjobs.o reqsim.o

clean:
	@rm -f builtinjobs.o reqsim reqsim.o

builtinjobs.o: builtinjobs.c reqsim.h
	@$(CC) -std=c89 -pedantic -c $<

reqsim.o: reqsim.c reqsim.h
	@$(CC) -std=c89 -pedantic -c $<
