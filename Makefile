.PHONY: clean

srs: raid_requests.o srs.o
	@gcc -o $@ $^

clean:
	@rm -f raid_requests.o srs srs.o

raid_requests.o: raid_requests.c srs.h
	@gcc -std=c89 -pedantic -c $<

srs.o: srs.c srs.h
	@gcc -std=c89 -pedantic -c $<
