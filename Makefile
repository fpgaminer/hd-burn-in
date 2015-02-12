all: hd-burn-in


hd-burn-in: hd-burn-in.c SFMT-src-1.4.1/SFMT.c
	$(CC) -I SFMT-src-1.4.1 -DSFMT_MEXP=19937 -DHAVE_SSE2=1 -std=c99 -O3 -Wall -msse2 -m64 -fno-strict-aliasing -o $@ $^


clean:
	rm -f hd-burn-in
