#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* open() */
#include <sys/stat.h>
#include <fcntl.h>

/* mmap() */
#include <sys/mman.h>

/* Custom C macros */
#include <defy/expect>
#include <defy/nil>

/* Hash table bucket structure */
struct Bucket {
	uint32_t val; /**< Value (word count) */
	uint16_t len; /**< Key length */
	char key[128 - sizeof (uint32_t) - sizeof (uint16_t)]; /**< Key */
};

/* Hash table */
static struct Bucket table[2097152 + 1] = { };

/* Convert character to lower-case */
static inline char lower(char chr) {
	assert(chr > 'A' && chr < 'Z' || chr > 'a' && chr < 'z');

	switch (chr) {
	case 'A' ... 'Z':
		return chr - ('A' - 'a');
	default:
		return chr;
	}
}

/* Extract 32bit word from input */
#define extract(data) ((((uint32_t) (data)[1]) << 8) + ((uint32_t) (data)[0]))

/* Hash function (Google's super fast hash) */
static uint32_t hash(char const *data, size_t len) {
	uint32_t hash = len;
	uint32_t temp;
	size_t rem;

	rem = len & 3;
	len >>= 2;

	while (len--) {
		hash += extract(data);
		temp = (extract(data+2) << 11) ^ hash;
		hash = (hash << 16) ^ temp;
		data += 2 * sizeof (uint16_t);
		hash += hash >> 11;
	}

	switch (rem) {
	case 3:
		hash += extract(data);
		hash ^= hash << 16;
		hash ^= data[sizeof (uint16_t)] << 18;
		hash += hash >> 11;
		break;

	case 2:
		hash += extract(data);
		hash ^= hash << 11;
		hash += hash >> 17;
		break;

	case 1:
		hash += *data;
		hash ^= hash << 10;
		hash += hash >> 1;
		break;
	}

	hash ^= hash << 3;
	hash += hash >> 5;
	hash ^= hash << 4;
	hash += hash >> 17;
	hash ^= hash << 25;
	hash += hash >> 6;

	return hash;
}

#define flip(integer) (((integer) % 2) ? -1 : 1)

/* Look up (possibly inserting) key in hash table */
struct Bucket *lookup(char const *key, size_t len) {
	assert(len <= sizeof table->key);

	size_t index = hash(key, len) % (sizeof table / sizeof *table);
	struct Bucket *bucket = table + index;

	/* Quadratic probing */
	/* FIXME: This code is broken */
	for (size_t iter = 0; iter < 64; ++iter) {
		/* Bucket empty? */
		if (!bucket->len) {
			memcpy(bucket->key, key, len);
			bucket->len = len;
			return bucket;
		}

		/* Check for collision */
		if (bucket->len == len && !memcmp(bucket->key, key, len))
			return bucket;

		bucket
			= table + (index + flip(iter) * (iter/2) * (iter/2))
			% (sizeof table / sizeof *table);
	}

	errno = ENOMEM;
	return (struct Bucket *) 0;
}

/* Parse input token and put it into hash table */
static void parseToken(char const *tok, size_t len) {
	if (unlikely(len) > sizeof (table->key)) {
		fputs("Sorry…", stderr);
		exit(EXIT_FAILURE);
	}

	/* Convert token to lower-case */
	char lcs[len];

	for (size_t iter = 0; iter < len; ++iter)
		lcs[iter] = lower(tok[iter]);

	/* Insert into hash table */
	struct Bucket *bucket = lookup(lcs, len);
	if (unlikely(!bucket)) {
		perror("lookup");
		exit(EXIT_FAILURE);
	}

	bucket->val += 1;
}

/* Determine the ten most common tokens */
static void top() {
	struct Bucket *topList[10] = { nil, nil, nil, nil, nil, nil, nil, nil, nil, nil };

	for (size_t iter = 0; iter < sizeof table / sizeof *table; ++iter) {
		if (table[iter].len) {
			for (size_t jter = 0; jter < sizeof topList / sizeof *topList; ++jter) {
				if (unlikely(!topList[jter])) {
					topList[jter] = table + iter;
					break;
				}

				else if (table[iter].val > topList[jter]->val) {
					for (size_t kter = sizeof topList / sizeof *topList - 1; kter > jter; --kter)
						topList[kter] = topList[kter - 1];
					topList[jter] = table + iter;
					break;
				}
			}
		}
	}

	/* Print tokens */
	for (size_t iter = 0; iter < sizeof topList / sizeof (struct Bucket *); ++iter) {
		if (topList[iter])
			printf("%*s: %" PRIu32 "\n",
				topList[iter]->len,
				topList[iter]->key,
				topList[iter]->val);
	}
}

int main(int argc, char *argv[]) {
	/* Check command-line arguments */
	if (unlikely(argc != 2)) {
		fprintf(stderr, "Usage: %s [file]\n", argv[0]);
		return EXIT_FAILURE;
	}

	/* Open input file */
	int fd = open(argv[1], O_RDONLY);
	if (unlikely(fd < 0)) {
		perror("open");
		return EXIT_FAILURE;
	}

	/* Determine input file size */
	off_t len = lseek(fd, 0, SEEK_END);
	if (unlikely(len < 0)) {
		perror("lseek");
		return EXIT_FAILURE;
	}

	/* Map input file to memory */
	void *map = mmap(nil, len, PROT_READ, MAP_SHARED, fd, 0);
	if (unlikely(map == MAP_FAILED)) {
		perror("mmap");
		return EXIT_FAILURE;
	}

	/* Advise OS about memory access pattern */
	if (unlikely(posix_madvise(map, len, POSIX_MADV_SEQUENTIAL | POSIX_MADV_WILLNEED)))
		perror("posix_madvise");

	/* Tokenise input */
	char const *base = (char const *) map;
	size_t head = 0;
	size_t tail = 0;

	while (tail < len) {
		switch (base[tail]) {
		case 'A' ... 'Z':
		case 'a' ... 'z':
			++tail;
			break;

		default:
			if (tail > head)
				parseToken(base + head, tail - head);
			head = ++tail;
			break;
		}
	}

	if (tail > head)
		parseToken(base + head, tail - head);

	top();
}
