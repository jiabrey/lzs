#ifndef _ASYNC_DMA_H_
#define _ASYNC_DMA_H_

enum ops_enum {
        OP_NULL       = 0,
        OP_FILL       = 1,
        OP_MEMCPY     = 2,
        OP_COMPRESS   = 3,
        OP_UNCOMPRESS = 4,
        OP_HASH       = 5,
        OP_COMPARE    = 6,
        OP_READ       = 7,
};
#ifdef __KERNEL__
/* 
 * this callback runing at interrupt level 
 */
typedef int (*async_cb_t)(void *priv, int err, uint32_t osize, uint32_t hash);

typedef struct {
        char   *buffer;
        int    use_sg;
        int    bufflen;
        dma_addr_t addr;
} sgbuf_t;

int async_submit(sgbuf_t *src, /* source data buffer */
                sgbuf_t *dst,  /* dest data buffer */
                async_cb_t cb, /* callback function */
                int ops,       /* ops */
                void *priv,    /* private for callback */
                int commit);   /* commit to hw immedate */

/* for debug */
void async_dump_register(void);
/* geting device cap */
/* bit 0 Reserved
 * bit 1 Reserved
 * bit 2 Reserved
 * bit 3 Reserved
 * bit 4 memory copy
 * bit 5 encode 
 * bit 6 decode 
 * bit 7 Reserved
 */
int  async_device_cap(void);
#endif
/* user space ioctl interface */
typedef struct {
        uint8_t ops;
        uint32_t src;
        uint32_t slen;
        uint32_t dst;
        uint32_t dlen;

        uint32_t flags;
        uint32_t err;
        uint32_t osize;
        uint32_t done;
        uint32_t hash;
} sioctl_t;

#define SIO_DEBUG    (1<<0)

#define SIOCTL_SUBMIT 0x2285

#endif
