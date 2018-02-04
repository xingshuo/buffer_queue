#include "buffer_queue.h"

#define BUFFER_CHAIN_SIZE sizeof(struct buffer_chain)
#define BUFFER_CHAIN_EXTRA(t, c) (t *)((struct buffer_chain *)(c) + 1)
#define CHAIN_SPACE_LEN(ch) ((ch)->buffer_len - ((ch)->misalign + (ch)->off))

static inline void
ZERO_CHAIN(struct buffer_queue *dst)
{
    dst->first = NULL;
    dst->last = NULL;
    dst->last_with_datap = &(dst)->first;
    dst->total_len = 0;
}

static inline void
buffer_chain_free(struct buffer_chain *chain)
{
    free(chain);
}

static void
buffer_chain_align(struct buffer_chain *chain)
{
    memmove(chain->buffer, chain->buffer + chain->misalign, chain->off);
    chain->misalign = 0;
}

static int
buffer_chain_should_realign(struct buffer_chain *chain, size_t datlen)
{
    return chain->buffer_len - chain->off >= datlen &&
        (chain->off < chain->buffer_len / 2) &&
        (chain->off <= MAX_TO_REALIGN_IN_EXPAND);
}

static void
buffer_queue_free_all_chains(struct buffer_chain *chain)
{
    struct buffer_chain *next;
    for (; chain; chain = next) {
        next = chain->next;
        buffer_chain_free(chain);
    }
}

static int
buffer_chains_all_empty(struct buffer_chain *chain)
{
    for (; chain; chain = chain->next) {
        if (chain->off)
            return 0;
    }
    return 1;
}

static struct buffer_chain **
buffer_queue_free_trailing_empty_chains(struct buffer_queue *buf)
{
    struct buffer_chain **ch = buf->last_with_datap;
    /* Find the first victim chain.  It might be *last_with_datap */
    while ((*ch) && ((*ch)->off != 0))
        ch = &(*ch)->next;

    if (*ch) {
        assert(buffer_chains_all_empty(*ch));
        buffer_queue_free_all_chains(*ch);
        *ch = NULL;
    }
    return ch;
}

static void
buffer_chain_insert(struct buffer_queue *buf, struct buffer_chain *chain)
{
    if (*buf->last_with_datap == NULL) {
        assert(buf->last_with_datap == &buf->first);
        assert(buf->first == NULL);
        buf->first = buf->last = chain;
    } else {
        struct buffer_chain **chp;
        chp = buffer_queue_free_trailing_empty_chains(buf);
        *chp = chain;
        if (chain->off)
            buf->last_with_datap = chp;
        buf->last = chain;
    }
    buf->total_len += chain->off;
}

static struct buffer_chain *
buffer_chain_new(size_t size)
{
    struct buffer_chain *chain;
    size_t to_alloc;

    if (size > BUFFER_CHAIN_MAX - BUFFER_CHAIN_SIZE)
        return (NULL);

    size += BUFFER_CHAIN_SIZE;

    if (size < BUFFER_CHAIN_MAX / 2) {
        to_alloc = MIN_BUFFER_SIZE;
        while (to_alloc < size) {
            to_alloc <<= 1;
        }
    } else {
        to_alloc = size;
    }

    if ((chain = malloc(to_alloc)) == NULL)
        return (NULL);

    memset(chain, 0, BUFFER_CHAIN_SIZE);
    chain->buffer_len = to_alloc - BUFFER_CHAIN_SIZE;
    chain->buffer = BUFFER_CHAIN_EXTRA(unsigned char, chain);
    return (chain);
}

struct buffer_queue *
buffer_queue_new(void)
{
    struct buffer_queue *buffer;

    buffer = calloc(1, sizeof(struct buffer_queue));
    if (buffer == NULL)
        return (NULL);
    BQ_LOCK_INIT(buffer);
    buffer->last_with_datap = &buffer->first;

    return (buffer);
}

void
buffer_queue_free(struct buffer_queue *buf)
{
    BQ_LOCK(buf);
    struct buffer_chain *chain, *next;
    for (chain = buf->first; chain != NULL; chain = next) {
        next = chain->next;
        buffer_chain_free(chain);
    }
    BQ_UNLOCK(buf);
    BQ_LOCK_FREE(buf);
    free(buf);
}

int
buffer_queue_add(struct buffer_queue *buf, const void *data_in, size_t datlen)
{
    struct buffer_chain *chain, *tmp;
    const unsigned char *data = data_in;
    size_t remain, to_alloc;
    int result = -1;

    BQ_LOCK(buf);

    if (buf->freeze_end) {
        goto done;
    }

    if (datlen > BQ_SIZE_MAX - buf->total_len) {
        goto done;
    }

    if (*buf->last_with_datap == NULL) {
        chain = buf->last;
    } else {
        chain = *buf->last_with_datap;
    }

    if (chain == NULL) {
        chain = buffer_chain_new(datlen);
        if (!chain)
            goto done;
        buffer_chain_insert(buf, chain);
    }

    remain = chain->buffer_len - (size_t)chain->misalign - chain->off;
    if (remain >= datlen) {
        memcpy(chain->buffer + chain->misalign + chain->off,
            data, datlen);
        chain->off += datlen;
        buf->total_len += datlen;
        goto out;
    } else if (buffer_chain_should_realign(chain, datlen)) {
        buffer_chain_align(chain);
        memcpy(chain->buffer + chain->off, data, datlen);
        chain->off += datlen;
        buf->total_len += datlen;
        goto out;
    }

    to_alloc = chain->buffer_len;
    if (to_alloc <= BUFFER_CHAIN_MAX_AUTO_SIZE/2)
        to_alloc <<= 1;
    if (datlen > to_alloc)
        to_alloc = datlen;
    tmp = buffer_chain_new(to_alloc);
    if (tmp == NULL)
        goto done;

    if (remain) {
        memcpy(chain->buffer + chain->misalign + chain->off,
            data, remain);
        chain->off += remain;
        buf->total_len += remain;
    }

    data += remain;
    datlen -= remain;

    memcpy(tmp->buffer, data, datlen);
    tmp->off = datlen;
    buffer_chain_insert(buf, tmp);

out:
    result = 0;
done:
    BQ_UNLOCK(buf);
    return result;
}

static inline struct buffer_chain *
buffer_chain_insert_new(struct buffer_queue *buf, size_t datlen)
{
    struct buffer_chain *chain;
    if ((chain = buffer_chain_new(datlen)) == NULL)
        return NULL;
    buffer_chain_insert(buf, chain);
    return chain;
}

static struct buffer_chain *
buffer_queue_expand_singlechain(struct buffer_queue *buf, size_t datlen)
{
    struct buffer_chain *chain, **chainp;
    struct buffer_chain *result = NULL;

    chainp = buf->last_with_datap;

    if (*chainp && CHAIN_SPACE_LEN(*chainp) == 0)
        chainp = &(*chainp)->next;

    chain = *chainp;

    if (chain == NULL) {
        goto insert_new;
    }

    if (CHAIN_SPACE_LEN(chain) >= datlen) {
        result = chain;
        goto ok;
    }

    /* If the chain is completely empty, just replace it by adding a new
     * empty chain. */
    if (chain->off == 0) {
        goto insert_new;
    }

    if (buffer_chain_should_realign(chain, datlen)) {
        buffer_chain_align(chain);
        result = chain;
        goto ok;
    }

    /* Would expanding this chunk be affordable and worthwhile? */
    if (CHAIN_SPACE_LEN(chain) < chain->buffer_len / 8 ||
        chain->off > MAX_TO_COPY_IN_EXPAND ||
        datlen >= (BUFFER_CHAIN_MAX - chain->off)) {
        /* It's not worth resizing this chain. Can the next one be
         * used? */
        if (chain->next && CHAIN_SPACE_LEN(chain->next) >= datlen) {
            /* Yes, we can just use the next chain (which should
             * be empty. */
            result = chain->next;
            goto ok;
        } else {
            /* No; append a new chain (which will free all
             * terminal empty chains.) */
            goto insert_new;
        }
    } else {
        /* Okay, we're going to try to resize this chain: Not doing so
         * would waste at least 1/8 of its current allocation, and we
         * can do so without having to copy more than
         * MAX_TO_COPY_IN_EXPAND bytes. */
        /* figure out how much space we need */
        size_t length = chain->off + datlen;
        struct buffer_chain *tmp = buffer_chain_new(length);
        if (tmp == NULL)
            goto err;

        /* copy the data over that we had so far */
        tmp->off = chain->off;
        memcpy(tmp->buffer, chain->buffer + chain->misalign,
            chain->off);
        /* fix up the list */
        assert(*chainp == chain);
        result = *chainp = tmp;

        if (buf->last == chain)
            buf->last = tmp;

        tmp->next = chain->next;
        buffer_chain_free(chain);
        goto ok;
    }

insert_new:
    result = buffer_chain_insert_new(buf, datlen);
    if (!result)
        goto err;

ok:
    assert(result);
    assert(CHAIN_SPACE_LEN(result) >= datlen);
err:
    return result;
}

int
buffer_queue_expand(struct buffer_queue *buf, size_t datlen)
{
    struct buffer_chain *chain;

    BQ_LOCK(buf);
    chain = buffer_queue_expand_singlechain(buf, datlen);
    BQ_UNLOCK(buf);
    return chain ? 0 : -1;
}

int
buffer_queue_prepend(struct buffer_queue *buf, const void *data, size_t datlen, int ignore_freeze)
{
    struct buffer_chain *chain, *tmp;
    int result = -1;

    BQ_LOCK(buf);

    if (buf->freeze_start && !ignore_freeze) {
        goto done;
    }

    if (datlen > BQ_SIZE_MAX - buf->total_len) {
        goto done;
    }

    chain = buf->first;

    if (chain == NULL) {
        chain = buffer_chain_new(datlen);
        if (!chain)
            goto done;
        buffer_chain_insert(buf, chain);
    }

    /* If this chain is empty, we can treat it as
     * 'empty at the beginning' rather than 'empty at the end' */
    if (chain->off == 0)
        chain->misalign = chain->buffer_len;

    if ((size_t)chain->misalign >= datlen) {
        /* we have enough space to fit everything */
        memcpy(chain->buffer + chain->misalign - datlen,
            data, datlen);
        chain->off += datlen;
        chain->misalign -= datlen;
        buf->total_len += datlen;
        goto out;
    } else if (chain->misalign) {
        /* we can only fit some of the data. */
        memcpy(chain->buffer,
            (char*)data + datlen - chain->misalign,
            (size_t)chain->misalign);
        chain->off += (size_t)chain->misalign;
        buf->total_len += (size_t)chain->misalign;
        datlen -= (size_t)chain->misalign;
        chain->misalign = 0;
    }

    /* we need to add another chain */
    if ((tmp = buffer_chain_new(datlen)) == NULL)
        goto done;
    buf->first = tmp;
    if (buf->last_with_datap == &buf->first)
        buf->last_with_datap = &tmp->next;

    tmp->next = chain;

    tmp->off = datlen;
    assert(datlen <= tmp->buffer_len);
    tmp->misalign = tmp->buffer_len - datlen;

    memcpy(tmp->buffer + tmp->misalign, data, datlen);
    buf->total_len += datlen;

out:
    result = 0;
done:
    BQ_UNLOCK(buf);
    return result;
}

ssize_t
buffer_queue_copyout(struct buffer_queue *buf, void *data_out, size_t datlen)
{
    struct buffer_chain *chain;  
    char *data = data_out;  
    size_t nread;  
    ssize_t result = 0;  
    
    BQ_LOCK(buf);

    chain = buf->first;  
  
    if (datlen >= buf->total_len)  
        datlen = buf->total_len;
  
    if (datlen == 0)  
        goto done;
  
    nread = datlen;  
    while (datlen && datlen >= chain->off) {
        memcpy(data, chain->buffer + chain->misalign, chain->off);  
        data += chain->off;  
        datlen -= chain->off;  
  
        chain = chain->next;  
    }  
  
    if (datlen) {
        memcpy(data, chain->buffer + chain->misalign, datlen);  
    }  
  
    result = nread;  
done:
    BQ_UNLOCK(buf);
    return result;  
}

int
buffer_queue_drain(struct buffer_queue *buf, size_t len)
{
    struct buffer_chain *chain, *next;
    size_t remaining, old_len;
    int result = 0;

    BQ_LOCK(buf);
    old_len = buf->total_len;

    if (old_len == 0)
        goto done;

    if (buf->freeze_start) {
        result = -1;
        goto done;
    }

    if (len >= old_len) {
        len = old_len;
        for (chain = buf->first; chain != NULL; chain = next) {
            next = chain->next;
            buffer_chain_free(chain);
        }
        ZERO_CHAIN(buf);
    } else {
        buf->total_len -= len;
        remaining = len;
        for (chain = buf->first;
             remaining >= chain->off;
             chain = next) {
            next = chain->next;
            remaining -= chain->off;

            if (chain == *buf->last_with_datap) {
                buf->last_with_datap = &buf->first;
            }
            if (&chain->next == buf->last_with_datap)
                buf->last_with_datap = &buf->first;
            buffer_chain_free(chain);
        }

        buf->first = chain;
        assert(remaining < chain->off);
        chain->misalign += remaining;
        chain->off -= remaining;
    }

done:
    BQ_UNLOCK(buf);
    return result;
}

int  
buffer_queue_remove(struct buffer_queue *buf, void *data_out, size_t datlen)  
{  
    ssize_t n;
    BQ_LOCK(buf);
    n = buffer_queue_copyout(buf, data_out, datlen);  
    if (n > 0) {
        if (buffer_queue_drain(buf, n)) {
            n = -1;
        }
    }
    BQ_UNLOCK(buf);
    return (int)n;  
}

size_t
buffer_queue_get_length(const struct buffer_queue *buf)
{
    size_t result;
    BQ_LOCK(buf);
    result = buf->total_len;
    BQ_UNLOCK(buf);
    return result;
}

void
buffer_queue_stat(const struct buffer_queue *buf)
{
    BQ_LOCK(buf);
    struct buffer_chain *chain,*next;
    int chain_used = 0;
    int chain_free = 0;
    printf("------buffer_queue stat begin---buffer_chain_size[%ld]-----\n",BUFFER_CHAIN_SIZE);
    for (chain = buf->first; chain != NULL; chain = next) {
        next = chain->next;
        if (chain == *buf->last_with_datap) {
            printf("last_data chain[%p]  misalign[%lu] off[%lu] buffer_len[%lu] next[%p]\n", chain, chain->misalign, chain->off, chain->buffer_len, next);
        }else {
            printf("chain[%p]  misalign[%lu] off[%lu] buffer_len[%lu] next[%p]\n", chain, chain->misalign, chain->off, chain->buffer_len, next);
        }
        if (chain->off != 0) {
            chain_used++;
        }else {
            chain_free++;
        }
    }
    printf("------buffer_queue stat end---total_chain [%d = %d(used) + %d(free)]------\n", chain_used+chain_free, chain_used, chain_free);
    BQ_UNLOCK(buf);
}

int
buffer_queue_freeze(struct buffer_queue *buffer, int start)
{
    BQ_LOCK(buffer);
    if (start)
        buffer->freeze_start = 1;
    else
        buffer->freeze_end = 1;
    BQ_UNLOCK(buffer);
    return 0;
}

int
buffer_queue_unfreeze(struct buffer_queue *buffer, int start)
{
    BQ_LOCK(buffer);
    if (start)
        buffer->freeze_start = 0;
    else
        buffer->freeze_end = 0;
    BQ_UNLOCK(buffer);
    return 0;
}