#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <SFMT.h>


static uint64_t get_device_size (char const *path)
{
	int fd = open (path, O_RDONLY);

	if (fd == -1)
	{
		fprintf (stderr, "ERROR: Unable to open '%s' for reading.\n", path);
		exit (-1);
	}

	off_t length = lseek (fd, 0, SEEK_END);

	if (length == (off_t)-1)
	{
		fprintf (stderr, "ERROR: Unable to read size of '%s'.\n", path);
		exit (-1);
	}

	close (fd);

	return (uint64_t)length;
}


static void print_time (int seconds)
{
	int hours = seconds / (60 * 60);
	seconds -= hours * 60 * 60;
	int minutes = seconds / 60;
	seconds -= minutes * 60;

	printf ("%02d:%02d:%02d", hours, minutes, seconds);
}


static void progress_update (struct timespec *last_time, uint64_t *last_value, double *average, uint64_t current, uint64_t total, bool reset)
{
	struct timespec now;
	const double alpha = 0.9;

	clock_gettime (CLOCK_MONOTONIC, &now);

	if (reset)
	{
		*last_time = now;
		*last_value = current;
		*average = 50.0;
	}

	if ((now.tv_sec - last_time->tv_sec) < 2)
		return;

	double seconds = now.tv_sec + (now.tv_nsec / 1000000000.0) - last_time->tv_sec - (last_time->tv_nsec / 1000000000.0);
	uint64_t value_diff = current - *last_value;

	//printf ("\r%lf %lu %lu  ", seconds, current, *last_value);

	*last_time = now;
	*last_value = current;

	double mbs = value_diff / (seconds * 1024 * 1024);
	*average = alpha * (*average) + (1.0 - alpha) * mbs;
	double percent = 100.0 * current / total;
	int time_remaining = (total - current) / ((*average) * 1024 * 1024);

	printf ("\r%.01f%%  %.01f MB/s   ", percent, *average);
	print_time (time_remaining);
	printf (" remaining        ");
	fflush (stdout);
}


static void fill_device (sfmt_t *sfmt, uint32_t seed, char const *path, size_t block_size, uint64_t block_count)
{
	int fd = open (path, O_RDWR);
	uint8_t *buffer = (uint8_t *)malloc (block_size);

	if (fd == -1)
	{
		fprintf (stderr, "ERROR: Unable to open '%s' for writing.\n", path);
		exit (-1);
	}

	/* Set up RNG */
	sfmt_init_gen_rand (sfmt, seed);

	/* Go to beginning */
	if (lseek (fd, 0, SEEK_SET) == (off_t)-1)
	{
		fprintf (stderr, "ERROR: Unable to seek to beginning of '%s'.\n", path);
		exit (-1);
	}

	/* Fill */
	printf ("Writing random data to disk...\n");

	struct timespec last_time;
	uint64_t last_value;
	double average;

	progress_update (&last_time, &last_value, &average, 0, block_count * block_size, true);

	for (uint64_t blocks_written = 0; blocks_written != block_count;)
	{
		sfmt_fill_array32 (sfmt, (uint32_t *)buffer, block_size / 4);

		if (write (fd, buffer, block_size) != block_size)
		{
			fprintf (stderr, "\nERROR: Error while writing to disk: %s\n", strerror (errno));
			exit (-1);
		}

		blocks_written += 1;

		progress_update (&last_time, &last_value, &average, blocks_written * block_size, block_count * block_size, false);
	}

	printf ("\n");

	/* Sync */
	if (syncfs (fd) == -1)
	{
		fprintf (stderr, "ERROR: Error while synchornizing data to disk: %s.\n", strerror (errno));
		exit (-1);
	}

	/* Done */
	if (close (fd) == -1)
	{
		fprintf (stderr, "ERROR: Unable to close device file: %s\n", strerror (errno));
		exit (-1);
	}
}


static void verify_device (sfmt_t *sfmt, uint32_t seed, char const *path, size_t block_size, uint64_t block_count)
{
	uint8_t *expected = (uint8_t *)malloc (block_size);
	uint8_t *buffer = (uint8_t *)malloc (block_size);
	int fd = open (path, O_RDWR);

	if (fd == -1)
	{
		fprintf (stderr, "ERROR: Unable to open '%s' for reading.\n", path);
		exit (-1);
	}

	/* Set up RNG */
	sfmt_init_gen_rand (sfmt, seed);

	/* Go to beginning */
	if (lseek (fd, 0, SEEK_SET) == (off_t)-1)
	{
		fprintf (stderr, "ERROR: Unable to seek to beginning of '%s'.\n", path);
		exit (-1);
	}

	/* Verify */
	printf ("Verifying random data on disk...\n");

	struct timespec last_time;
	uint64_t last_value;
	double average;

	progress_update (&last_time, &last_value, &average, 0, block_count * block_size, true);

	for (uint64_t blocks_read = 0; blocks_read != block_count;)
	{
		sfmt_fill_array32 (sfmt, (uint32_t *)expected, block_size / 4);
		if (read (fd, buffer, block_size) != block_size)
		{
			fprintf (stderr, "\nERROR: Error while reading from device: %s\n", strerror (errno));
			exit (-1);
		}

		if (memcmp (expected, buffer, block_size))
		{
			fprintf (stderr, "\nERROR: Data on disk does not match what was expected.\n");
			exit (-1);
		}

		blocks_read += 1;

		progress_update (&last_time, &last_value, &average, blocks_read * block_size, block_count * block_size, false);
	}

	printf ("\n");

	/* Done */
	if (close (fd) == -1)
	{
		fprintf (stderr, "ERROR: Unable to close device file: %s\n", strerror (errno));
		exit (-1);
	}
}


int main (int argc, char *argv[])
{
	sfmt_t sfmt;
	uint32_t seed;
	char const *device_path = "";
	size_t block_size = 4096;

	/* Options */
	if (argc != 2)
	{
		fprintf (stderr, "USAGE: %s DEVICE_PATH\n", argv[0]);
		fprintf (stderr, "EXAMPLE: %s /dev/sdc\n", argv[0]);
		exit (-1);
	}

	device_path = argv[1];

	seed = (uint32_t)time (NULL);

	if ((block_size & 3) != 0)
	{
		fprintf (stderr, "ERROR: Block Size must be a multiple of 4.\n");
		exit (-1);
	}

	if (sfmt_get_min_array_size32 (&sfmt) > (block_size / 4))
	{
		fprintf (stderr, "ERROR: sfmt min array size too large.\n");
		exit (-1);
	}

	/* Get device details */
	uint64_t device_size = get_device_size (device_path);
	uint64_t block_count = device_size / block_size;

	if (block_count * block_size != device_size)
	{
		fprintf (stderr, "ERROR: Device is not a multiple of block size.\n");
		exit (-1);
	}

	printf ("%s is %ld bytes, with %ld blocks of size %zu.\n\n", device_path, device_size, block_count, block_size);

	/* Fill */
	fill_device (&sfmt, seed, device_path, block_size, block_count);

	/* Verify */
	verify_device (&sfmt, seed, device_path, block_size, block_count);

	printf ("Burn-in complete.  Entire disk verified successfully.\n");

	return 0;
}
