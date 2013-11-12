# We don't want headers on the command line, so for now the automatic variable
# $^ is not used. Perhaps I can make a rule for each .c file and can use $<.
srs: srs.c stripe_requests.c srs.h
	@gcc -std=c89 -pedantic -o $@ srs.c stripe_requests.c
