#ifndef _BUFFER_QUEUE_H_
#define _BUFFER_QUEUE_H_

#include "bqlock.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h> 
#include <string.h>
#include <time.h>
#include <assert.h>

#define BQ_SIZE_MAX ((((int64_t)0xffffffffUL) << 32) | 0xffffffffUL)
#define BUFFER_CHAIN_MAX ((((int64_t) 0x7fffffffL) << 32) | 0xffffffffL)

#ifndef BQ_USR_DEF //use libevent define
    #define MIN_BUFFER_SIZE 512
    #define MAX_TO_COPY_IN_EXPAND 4096
    #define MAX_TO_REALIGN_IN_EXPAND 2048
    #define BUFFER_CHAIN_MAX_AUTO_SIZE 4096
#else //user customize
    #define MIN_BUFFER_SIZE 8
    #define MAX_TO_COPY_IN_EXPAND 64
    #define MAX_TO_REALIGN_IN_EXPAND 32
    #define BUFFER_CHAIN_MAX_AUTO_SIZE 64
#endif

struct buffer_chain {
    struct buffer_chain *next;
    size_t buffer_len;
    uint64_t misalign;
    size_t off;
    unsigned char *buffer;
};

struct buffer_queue {
    struct buffer_chain *first;
    struct buffer_chain *last;
    struct buffer_chain **last_with_datap;
    size_t total_len;
    struct bqlock lock;
    unsigned freeze_start;
    unsigned freeze_end;
};

struct buffer_queue* buffer_queue_new(void);
void buffer_queue_free(struct buffer_queue *buf);
int buffer_queue_add(struct buffer_queue *buf, const void *data_in, size_t datlen);
int buffer_queue_prepend(struct buffer_queue *buf, const void *data, size_t datlen, int ignore_freeze);
int buffer_queue_expand(struct buffer_queue *buf, size_t datlen);
ssize_t buffer_queue_copyout(struct buffer_queue *buf, void *data_out, size_t datlen);
int buffer_queue_drain(struct buffer_queue *buf, size_t len);
int buffer_queue_remove(struct buffer_queue *buf, void *data_out, size_t datlen);
size_t buffer_queue_get_length(const struct buffer_queue *buf);
void buffer_queue_stat(const struct buffer_queue *buf);
int buffer_queue_freeze(struct buffer_queue *buffer, int start);
int buffer_queue_unfreeze(struct buffer_queue *buffer, int start);

#endif