/*
 * Copyright 2012 Jared Boone <jared@sharebrained.com>
 * Copyright 2013-2014 Benjamin Vernoux <titanmkd@gmail.com>
 *
 * This file is part of HackRF.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "common.h"
#include "getopt.h"
#include "hackrf.h"
#include "hackrf_transfer.h"
#include "queue.h"

#define _FILE_OFFSET_BITS 64

#ifndef bool
typedef int bool;
#define true 1
#define false 0
#endif

#ifdef _WIN32
#include <windows.h>

#ifdef _MSC_VER

#ifdef _WIN64
typedef int64_t ssize_t;
#else
typedef int32_t ssize_t;
#endif

#define strtoull _strtoui64
#define snprintf _snprintf

#endif
#endif

#if defined(__GNUC__)
#include <unistd.h>
#include <sys/time.h>
#endif

#include <signal.h>

#define FD_BUFFER_SIZE (8*1024)

#define FREQ_ONE_MHZ (1000000ll)

#define DEFAULT_FREQ_HZ (1575420000ll) /* 1575420000 */
#define FREQ_MIN_HZ	(0ull) /* 0 Hz */
#define FREQ_MAX_HZ	(7250000000ll) /* 7250MHz */
#define IF_MIN_HZ (2150000000ll)
#define IF_MAX_HZ (2750000000ll)
#define LO_MIN_HZ (84375000ll)
#define LO_MAX_HZ (5400000000ll)
#define DEFAULT_LO_HZ (1000000000ll)

#define DEFAULT_SAMPLE_RATE_HZ (10000000) /* 10MHz default sample rate */

#define DEFAULT_BASEBAND_FILTER_BANDWIDTH (5000000) /* 5MHz default */

#define SAMPLES_TO_XFER_MAX (0x8000000000000000ull) /* Max value */

#define BASEBAND_FILTER_BW_MIN (1750000)  /* 1.75 MHz min value */
#define BASEBAND_FILTER_BW_MAX (28000000) /* 28 MHz max value */

#if defined _WIN32
#define sleep(a) Sleep( (a*1000) )
#endif

typedef enum {
	TRANSCEIVER_MODE_OFF = 0,
	TRANSCEIVER_MODE_RX = 1,
	TRANSCEIVER_MODE_TX = 2,
	TRANSCEIVER_MODE_SS = 3,
} transceiver_mode_t;

typedef enum {
	HW_SYNC_MODE_OFF = 0,
	HW_SYNC_MODE_ON = 1,
} hw_sync_mode_t;

/* WAVE or RIFF WAVE file format containing IQ 2x8bits data for HackRF compatible with SDR# Wav IQ file */
typedef struct
{
	char groupID[4]; /* 'RIFF' */
	uint32_t size; /* File size + 8bytes */
	char riffType[4]; /* 'WAVE'*/
} t_WAVRIFF_hdr;

#define FormatID "fmt "   /* chunkID for Format Chunk. NOTE: There is a space at the end of this ID. */

typedef struct {
	char		chunkID[4]; /* 'fmt ' */
	uint32_t	chunkSize; /* 16 fixed */

	uint16_t	wFormatTag; /* 1 fixed */
	uint16_t	wChannels;  /* 2 fixed */
	uint32_t	dwSamplesPerSec; /* Freq Hz sampling */
	uint32_t	dwAvgBytesPerSec; /* Freq Hz sampling x 2 */
	uint16_t	wBlockAlign; /* 2 fixed */
	uint16_t	wBitsPerSample; /* 8 fixed */
} t_FormatChunk;

typedef struct
{
	char		chunkID[4]; /* 'data' */
	uint32_t	chunkSize; /* Size of data in bytes */
	/* Samples I(8bits) then Q(8bits), I, Q ... */
} t_DataChunk;

static transceiver_mode_t transceiver_mode = TRANSCEIVER_MODE_RX;

#define U64TOA_MAX_DIGIT (31)
typedef struct
{
	char data[U64TOA_MAX_DIGIT + 1];
} t_u64toa;

t_u64toa ascii_u64_data1;
t_u64toa ascii_u64_data2;

static float
TimevalDiff(const struct timeval *a, const struct timeval *b)
{
	return (a->tv_sec - b->tv_sec) + 1e-6f * (a->tv_usec - b->tv_usec);
}

int parse_u64(char* s, uint64_t* const value) {
	uint_fast8_t base = 10;
	char* s_end;
	uint64_t u64_value;

	if (strlen(s) > 2) {
		if (s[0] == '0') {
			if ((s[1] == 'x') || (s[1] == 'X')) {
				base = 16;
				s += 2;
			}
			else if ((s[1] == 'b') || (s[1] == 'B')) {
				base = 2;
				s += 2;
			}
		}
	}

	s_end = s;
	u64_value = strtoull(s, &s_end, base);
	if ((s != s_end) && (*s_end == 0)) {
		*value = u64_value;
		return HACKRF_SUCCESS;
	}
	else {
		return HACKRF_ERROR_INVALID_PARAM;
	}
}

int parse_u32(char* s, uint32_t* const value) {
	uint_fast8_t base = 10;
	char* s_end;
	uint64_t ulong_value;

	if (strlen(s) > 2) {
		if (s[0] == '0') {
			if ((s[1] == 'x') || (s[1] == 'X')) {
				base = 16;
				s += 2;
			}
			else if ((s[1] == 'b') || (s[1] == 'B')) {
				base = 2;
				s += 2;
			}
		}
	}

	s_end = s;
	ulong_value = strtoul(s, &s_end, base);
	if ((s != s_end) && (*s_end == 0)) {
		*value = (uint32_t)ulong_value;
		return HACKRF_SUCCESS;
	}
	else {
		return HACKRF_ERROR_INVALID_PARAM;
	}
}

/* Parse frequencies as doubles to take advantage of notation parsing */
int parse_frequency_i64(char* optarg, char* endptr, int64_t* value) {
	*value = (int64_t)strtod(optarg, &endptr);
	if (optarg == endptr) {
		return HACKRF_ERROR_INVALID_PARAM;
	}
	return HACKRF_SUCCESS;
}

int parse_frequency_u32(char* optarg, char* endptr, uint32_t* value) {
	*value = (uint32_t)strtod(optarg, &endptr);
	if (optarg == endptr) {
		return HACKRF_ERROR_INVALID_PARAM;
	}
	return HACKRF_SUCCESS;
}

static char *stringrev(char *str)
{
	char *p1, *p2;

	if (!str || !*str)
		return str;

	for (p1 = str, p2 = str + strlen(str) - 1; p2 > p1; ++p1, --p2)
	{
		*p1 ^= *p2;
		*p2 ^= *p1;
		*p1 ^= *p2;
	}
	return str;
}

char* u64toa(uint64_t val, t_u64toa* str)
{
#define BASE (10ull) /* Base10 by default */
	uint64_t sum;
	int pos;
	int digit;
	int max_len;
	char* res;

	sum = val;
	max_len = U64TOA_MAX_DIGIT;
	pos = 0;

	do
	{
		digit = (sum % BASE);
		str->data[pos] = digit + '0';
		pos++;

		sum /= BASE;
	} while ((sum > 0) && (pos < max_len));

	if ((pos == max_len) && (sum > 0))
		return NULL;

	str->data[pos] = '\0';
	res = stringrev(str->data);

	return res;
}

volatile uint32_t byte_count = 0;

bool hw_sync = false;
uint32_t hw_sync_enable = 0;

struct timeval time_start;
struct timeval t_start;

bool automatic_tuning = false;
int64_t freq_hz;

bool amp = false;
uint32_t amp_enable;

bool sample_rate = false;
uint32_t sample_rate_hz;

queueHead *queue;
message *msg = NULL;
size_t bytesRead;

int tx_callback(hackrf_transfer* transfer) {
	//fprintf(stderr, "TXCALLBACK\n");
	size_t bytes_to_read = 0;
	size_t bytes_in_buffer = 0;
	byte_count += transfer->valid_length;
	bytes_to_read = transfer->valid_length;

	while (bytes_in_buffer < bytes_to_read) {
		if (NULL == msg) {
			msg = queuePop(queue);
			bytesRead = 0;
		}

		//fprintf(stderr, "Reading from %p buffer: %zd %zd %zd %zd %d\n", msg, msg->length, bytesRead, bytes_to_read, bytes_in_buffer, queue->numElements);

		if (NULL == msg) {
			fprintf(stderr, "Nothing in buffer!");
			return -1;
		}

		// more than enough left
		if (bytes_to_read - bytes_in_buffer < msg->length - bytesRead) {
			memcpy(transfer->buffer + bytes_in_buffer, msg->buffer + bytesRead, bytes_to_read - bytes_in_buffer);
			bytesRead += bytes_to_read - bytes_in_buffer;
			return 0;
		}

		// less than enough left
		else {
			memcpy(transfer->buffer + bytes_in_buffer, msg->buffer + bytesRead, msg->length - bytesRead);
			bytes_in_buffer += msg->length - bytesRead;
			bytesRead += msg->length - bytesRead;
			free(msg->buffer);
			free(msg);
			msg = NULL;
			bytesRead = 0;
		}
	}
	return 0;
}

static hackrf_device* device = NULL;

DWORD startHackRF(LPVOID lpParam) {
	const char* serial_number = NULL;
	char* endptr = NULL;
	int result;
	struct hackrfThreadArgs *hta;
	struct tm * timeinfo = NULL;
	struct timeval t_end;
	float time_diff;
	unsigned int lna_gain = 8, vga_gain = 20, txvga_gain = 0;

	hta = (hackrfThreadArgs *)lpParam;
	queue = hta->queue;

	result = HACKRF_SUCCESS;

	Sleep(1000);

	if (NULL == hta || NULL == hta->argv) {
		running = 0;
		return 1;
	}
	optreset = 1;
	optind = 1;
	while ((result = getopt(hta->argc, hta->argv, "e:u:g:c:l:s:b:T:t:d:ivwf:a:x:h")) != -1)
	{
		switch (result) {
		case 'f':
			automatic_tuning = true;
			result = parse_frequency_i64(optarg, endptr, &freq_hz);
			break;

		case 'a':
			amp = true;
			result = parse_u32(optarg, &amp_enable);
			break;

		case 'x':
			result = parse_u32(optarg, &txvga_gain);
			break;

		case 's':
			sample_rate = true;
			result = parse_frequency_u32(optarg, endptr, &sample_rate_hz);
			break;
		}
	}

	if (automatic_tuning) {
		if (0 > freq_hz > FREQ_MAX_HZ)
		{
			fprintf(stderr, "argument error: freq_hz shall be between %s and %s.\n",
				u64toa(FREQ_MIN_HZ, &ascii_u64_data1),
				u64toa(FREQ_MAX_HZ, &ascii_u64_data2));
			goto done;
		}
	}
	else {
		/* Use default freq */
		freq_hz = DEFAULT_FREQ_HZ;
		automatic_tuning = true;
	}

	if (amp) {
		if (amp_enable > 1)
		{
			fprintf(stderr, "argument error: amp_enable shall be 0 or 1.\n");
			goto done;
		}
	}

	if (sample_rate == false)
	{
		sample_rate_hz = DEFAULT_SAMPLE_RATE_HZ;
	}

	transceiver_mode = TRANSCEIVER_MODE_TX;

	result = hackrf_init();
	if (result != HACKRF_SUCCESS) {
		fprintf(stderr, "hackrf_init() failed: %s (%d)\n", hackrf_error_name(result), result);
		goto done;
	}

	result = hackrf_open_by_serial(serial_number, &device);
	if (result != HACKRF_SUCCESS) {
		fprintf(stderr, "hackrf_open() failed: %s (%d)\n", hackrf_error_name(result), result);
		goto done;
	}
	   
	fprintf(stderr, "call hackrf_set_sample_rate(%u Hz/%.03f MHz)\n", sample_rate_hz, ((float)sample_rate_hz / (float)FREQ_ONE_MHZ));
	result = hackrf_set_sample_rate(device, sample_rate_hz);
	if (result != HACKRF_SUCCESS) {
		fprintf(stderr, "hackrf_set_sample_rate() failed: %s (%d)\n", hackrf_error_name(result), result);
		goto done;
	}

	fprintf(stderr, "call hackrf_set_hw_sync_mode(%d)\n", hw_sync_enable);
	result = hackrf_set_hw_sync_mode(device, hw_sync_enable ? HW_SYNC_MODE_ON : HW_SYNC_MODE_OFF);
	if (result != HACKRF_SUCCESS) {
		fprintf(stderr, "hackrf_set_hw_sync_mode() failed: %s (%d)\n", hackrf_error_name(result), result);
		goto done;
	}

	result = hackrf_set_txvga_gain(device, txvga_gain);
	result |= hackrf_start_tx(device, tx_callback, NULL);
	if (result != HACKRF_SUCCESS) {
		fprintf(stderr, "hackrf_start_?x() failed: %s (%d)\n", hackrf_error_name(result), result);
		goto done;
	}

	fprintf(stderr, "call hackrf_set_freq(%s Hz/%.03f MHz)\n", u64toa(freq_hz, &ascii_u64_data1), ((double)freq_hz / (double)FREQ_ONE_MHZ));
	result = hackrf_set_freq(device, freq_hz);
	if (result != HACKRF_SUCCESS) {
		fprintf(stderr, "hackrf_set_freq() failed: %s (%d)\n", hackrf_error_name(result), result);
		goto done;
	}

	if (amp) {
		fprintf(stderr, "call hackrf_set_amp_enable(%u)\n", amp_enable);
		result = hackrf_set_amp_enable(device, (uint8_t)amp_enable);
		if (result != HACKRF_SUCCESS) {
			fprintf(stderr, "hackrf_set_amp_enable() failed: %s (%d)\n", hackrf_error_name(result), result);
			goto done;
		}
	}

	gettimeofday(&t_start, NULL);
	gettimeofday(&time_start, NULL);

	fprintf(stderr, "Stop with Ctrl-C\n");
	while ((hackrf_is_streaming(device) == HACKRF_TRUE) && running == true)
	{
		uint32_t byte_count_now;
		struct timeval time_now;
		float time_difference, rate;
		sleep(1);
		gettimeofday(&time_now, NULL);

		byte_count_now = byte_count;
		byte_count = 0;

		time_difference = TimevalDiff(&time_now, &time_start);
		rate = (float)byte_count_now / time_difference;
		if (byte_count_now == 0 && hw_sync == true && hw_sync_enable != 0) {
			fprintf(stderr, "Waiting for sync...\n");
		}
		else {
			//fprintf(stderr, "%4.1f MiB / %5.3f sec = %4.1f MiB/second\n",
			//	(byte_count_now / 1e6f), time_difference, (rate / 1e6f));
		}

		time_start = time_now;

		if (byte_count_now == 0 && (hw_sync == false || hw_sync_enable == 0)) {
			fprintf(stderr, "\nCouldn't transfer any bytes for one second.\n");
			break;
		}
	}

	done:

	running = false;
	result = hackrf_is_streaming(device);
	fprintf(stderr, "\nExiting... hackrf_is_streaming() result: %s (%d)\n", hackrf_error_name(result), result);

	gettimeofday(&t_end, NULL);
	time_diff = TimevalDiff(&t_end, &t_start);
	fprintf(stderr, "Total time: %5.5f s\n", time_diff);

	if (device != NULL) {
		result = hackrf_stop_tx(device);
		if (result != HACKRF_SUCCESS) {
			fprintf(stderr, "hackrf_stop_tx() failed: %s (%d)\n", hackrf_error_name(result), result);
		}
		else {
			fprintf(stderr, "hackrf_stop_tx() done\n");
		}

		result = hackrf_close(device);
		if (result != HACKRF_SUCCESS) {
			fprintf(stderr, "hackrf_close() failed: %s (%d)\n", hackrf_error_name(result), result);
		}
		else {
			fprintf(stderr, "hackrf_close() done\n");
		}

		hackrf_exit();
		fprintf(stderr, "hackrf_exit() done\n");
	}

	return 0;
}