/*
 * lzs dma module for kernel 2.6.10 
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/uio.h>
#include <asm/uaccess.h>
#include <asm/dma-mapping.h>
#include <asm/scatterlist.h>

#include "async_dma.h"
#include "lzf_chip.h"

/* Debug */
#define dprintk(format, a...) \
        do { \
                if (debug) printk("%s:%d "format, __FUNCTION__, __LINE__, ##a);\
        } while (0)

static int debug = 1;

/* backport hexdump.c */
enum {
        DUMP_PREFIX_NONE,
        DUMP_PREFIX_ADDRESS,
        DUMP_PREFIX_OFFSET
};
#define bool int
#define hex_asc(x)  "0123456789abcdef"[x]
#include "hexdump.c"

enum {
        MIN_QUEUE = 64,
        MAX_QUEUE = 128,
};

static kmem_cache_t *_cache, *job_cache;
#define MYNAM "lzf_dma"

typedef struct {
        volatile uint8_t __iomem *address;
        uint32_t value;
} hw_register_t;

struct lzf_device {
        uint32_t bases;
        int base_size;
        struct pci_dev *dev;

        volatile uint8_t __iomem *mmr_base;
        int irq;

        /* registers */
        struct {
                hw_register_t CCR,
                              CSR,
                              NDAR,
                              DAR;
        } R;

        /* queue and lock */
        spinlock_t desc_lock;
        struct list_head used_head, free_head;

        wait_queue_head_t wait;
        atomic_t queue;

};

typedef struct {
        job_desc_t *desc;
        dma_addr_t addr;

        int cookie;
        struct list_head entry;

        buf_desc_t *src, *dst;
        res_desc_t *res;

        void *priv;
        async_cb_t cb;

        sgbuf_t *src_buf, *dst_buf;
        
        dma_addr_t h_addr;
} job_entry_t;

static job_entry_t *new_job_entry(struct lzf_device *ioc)
{
        job_entry_t *p;

        p = kmem_cache_alloc(job_cache, GFP_KERNEL);
        if (p == NULL)
                return NULL;
        memset(p, 0, sizeof(*p));

        p->desc = pci_alloc_consistent(ioc->dev, PAGE_SIZE, &p->addr);
        if (p->desc == NULL)
                return NULL;

        p->res = (void *)((char *)p->desc + 2048);
        p->desc->ctl_addr = p->addr + 2048;

        return p;
}

static job_entry_t *get_job_entry(struct lzf_device *ioc)
{
        job_entry_t *p = NULL;
        
        BUG_ON(in_irq());
        dprintk(MYNAM ": queue %x\n", atomic_read(&ioc->queue));
        wait_event(ioc->wait, atomic_read(&ioc->queue) < MIN_QUEUE);

        spin_lock_bh(&ioc->desc_lock);
        if (!list_empty(&ioc->free_head)) {
                p = container_of(ioc->free_head.next, job_entry_t, entry);
                list_del(&p->entry);
        }
        spin_unlock_bh(&ioc->desc_lock);
        atomic_inc(&ioc->queue);

        memset(p->desc, 0, sizeof(job_desc_t));
        memset(p->res, 0, sizeof(res_desc_t));

        return p;
}

static buf_desc_t * map_bufs(struct lzf_device *ioc, sgbuf_t *s, 
                dma_addr_t *h_addr, int dir)
{
        int bytes_to_go = s->bufflen;
        buf_desc_t *b = NULL, *prev = NULL, *h = NULL;
        dma_addr_t addr = 0;
        struct scatterlist *sgl = NULL;

        if (s->use_sg == 0) {
                addr = pci_map_single(ioc->dev, s->buffer, s->bufflen, dir);
                if (bytes_to_go <= LZF_MAX_SG_ELEM_LEN) {
                        dma_addr_t hw_addr;
                        b = pci_alloc_consistent(ioc->dev, 64, &hw_addr);
                        b->desc = bytes_to_go;
                        b->desc|= LZF_SG_LAST;
                        b->u[2] = hw_addr;
                        b->u[3] = addr;
                        b->u[4] = (uint32_t)prev;
                        return b;
                }
        } else {
                sgl = (struct scatterlist *)s->buffer;
                pci_map_sg(ioc->dev, sgl, s->use_sg, dir);
        }

        while (bytes_to_go > 0) {
                int this_mapping_len = sgl ? sg_dma_len(sgl) : bytes_to_go;
                int offset = 0;
                while (this_mapping_len > 0) {
                        dma_addr_t hw_addr;
                        int this_len = min_t(int, LZF_MAX_SG_ELEM_LEN, 
                                        this_mapping_len);
                        b = pci_alloc_consistent(ioc->dev, 64, &hw_addr);
                        b->desc = this_len;
                        b->u[2] = hw_addr;
                        b->u[3] = (sgl ? sg_dma_address(sgl) : addr) + offset;
                        b->u[4] = (uint32_t)prev;
                        if (prev == NULL)
                                h = b;
                        prev = b;
                        this_mapping_len -= this_len;
                        bytes_to_go -= this_len;
                        offset += this_len;
                }
                if (sgl)
                        sgl++;
        }
        prev->desc |= LZF_SG_LAST;

        return h;
}

static int dc_ay[] = {
        [OP_FILL]       = DC_FILL,
        [OP_MEMCPY]     = DC_MEMCPY,
        [OP_COMPRESS]   = DC_COMPRESS,
        [DC_UNCOMPRESS] = OP_UNCOMPRESS, 
};

int async_submit(sgbuf_t *src, sgbuf_t *dst, async_cb_t cb, int ops, void *p)
{
        int res = 0;
        job_entry_t *d, *prev;
        LIST_HEAD(new_chain);
        struct lzf_device *ioc = NULL; /* TODO */

        d = get_job_entry(ioc);
        d->src_buf = src;
        d->dst_buf = dst;

        /* fill the hw desc */
        d->desc->next_desc = 0;
        d->desc->dc_fc  = dc_ay[ops];
        d->src = map_bufs(ioc, src, &d->desc->src_desc, PCI_DMA_TODEVICE);
        d->dst = map_bufs(ioc, dst, &d->desc->dst_desc, PCI_DMA_FROMDEVICE);

        spin_lock_bh(&ioc->desc_lock);
        prev = container_of(ioc->used_head.prev, job_entry_t, entry);
        prev->desc->next_desc = d->addr;
        prev->desc->dc_fc    |= DC_CONT;

        list_add(&d->entry, &new_chain);
        __list_splice(&new_chain, ioc->used_head.prev);

        writel(CCR_RESUME|CCR_ENABLE, ioc->R.CCR.address);
        spin_unlock_bh(&ioc->desc_lock);

        return res;
}
EXPORT_SYMBOL(async_submit);

static int unmap_bufs(struct lzf_device *ioc, buf_desc_t *d, int dir,
                sgbuf_t *s, dma_addr_t h_addr)
{
        int res = 0;

        while (d) {
                buf_desc_t *n;
                dma_addr_t addr = d->u[3];
                n = (buf_desc_t *)d->u[4];
                /* free it */
                addr = d->u[2];
                pci_free_consistent(ioc->dev, 64, d, addr);
                d = n;
        }
        /* unmap data buffer */
        if (s->use_sg) 
                pci_unmap_single(ioc->dev, h_addr, s->bufflen, dir);
        else
                pci_unmap_sg(ioc->dev, (struct scatterlist *)s->buffer,
                                s->use_sg, dir);

        return res;
}

/* 
 * Psuedo code:
 *  pci_unmap all buffers, include src, dst, result
 *  doing callback.
 *  freeing the resource.
 */
static int do_job_one(struct lzf_device *ioc, job_entry_t *d)
{
        int res = 0;

        /* unmap result data */
        unmap_bufs(ioc, d->src, PCI_DMA_TODEVICE, d->src_buf, d->h_addr);
        unmap_bufs(ioc, d->dst, PCI_DMA_FROMDEVICE, d->dst_buf, d->h_addr);

        d->cb(d->priv, d->res->err, d->res->ocnt);

        atomic_dec(&ioc->queue);
        wake_up_all(&ioc->wait);

        return res;
}

static int do_jobs(struct lzf_device *ioc)
{
        job_entry_t *d, *t;
        uint32_t phys_complete;
        LIST_HEAD(head);
        int res = 0;

        /*if (!spin_trylock_bh(&ioc->desc_lock))
                return;*/
        phys_complete = readl(ioc->R.DAR.address);
        dprintk("phys %x\n", phys_complete);
        list_for_each_entry_safe(d, t, &ioc->used_head, entry) {
                dprintk("addr %x, cookie %x\n", d->addr, d->cookie);
                if (d->cookie)
                        list_add_tail(&d->entry, &head);
                if (d->addr != phys_complete) {
                        list_del(&d->entry);
                        list_add_tail(&d->entry, &head);
                } else {
                        d->cookie = 0;
                        break;
                }
        }
        /*spin_unlock_bh(&ioc->desc_lock);*/

        list_for_each_entry_safe(d, t, &head, entry) {
                dprintk("addr %x, cookie %x\n", d->addr, d->cookie);
                list_del(&d->entry);
                do_job_one(ioc, d);
        }

        return res;
}

static int lzf_intr_handler(int irq, void *p, struct pt_regs *regs)
{
        uint32_t val = 0;
        int res = IRQ_NONE;
        struct lzf_device *ioc = p;

        val = readl(ioc->R.CSR.address);
        if ((val & CSR_INTP) == 0) { /* interrupt pending */
                goto out;
        }
        res = IRQ_HANDLED;

        /* clear irq flags */
        val = readl(ioc->R.CCR.address);
        val |= CCR_C_INTP;
        writel(val, ioc->R.CCR.address);
        wmb();
        val = readl(ioc->R.CCR.address);

        /* call the finished jobs */
        do_jobs(ioc);

out:
        return res;
}

static void start_null_desc(struct lzf_device *ioc)
{
        job_entry_t *d;

        /* reset device */
        writel(0, ioc->R.CCR.address);

        d = get_job_entry(ioc);
        d->desc->next_desc = 0;
        d->desc->dc_fc     = DC_NULL|DC_INTR_EN;
        d->addr = pci_map_single(ioc->dev, d->desc, 
                        sizeof(*d->desc), PCI_DMA_TODEVICE);

        spin_lock_bh(&ioc->desc_lock);
        d->cookie = 0;
        list_add_tail(&d->entry, &ioc->used_head);
        spin_unlock_bh(&ioc->desc_lock);

        writel(d->addr, ioc->R.NDAR.address);
        writel(2, ioc->R.CCR.address);
}

static int __devinit lzf_probe(struct pci_dev  *pdev, 
                const struct pci_device_id *id)
{
        struct lzf_device *ioc;
        int res = -ENODEV, i = 0;
        uint32_t val;
        struct resource *r;

        if (pci_enable_device(pdev))
                return res;
        ioc = kmalloc(sizeof(*ioc), GFP_KERNEL);

        ioc->bases = pci_resource_start(pdev, 1);
        ioc->base_size = pci_resource_len(pdev, 1);
        ioc->dev = pdev;
        ioc->irq = pdev->irq;
       
        /* enable mmio and master */
        pci_read_config_dword(pdev, PCI_COMMAND, &val);
        val |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
        pci_write_config_dword(pdev, PCI_COMMAND, val);
        /* Set burst length */
        pci_write_config_dword(pdev, PCI_CACHE_LINE_SIZE, 0x4004);
        val = 1<<0; /* MRL */
        val |=1<<1; /* prefetch */
        pci_write_config_dword(pdev, 0x184, val);
        /* base address */
        pci_write_config_dword(pdev, 0x188, 0x0);
        /* address mask */
        pci_write_config_dword(pdev, 0x18c, 0x80000000);
        /* intr enable */
        pci_write_config_dword(pdev, 0x1ec, 0x1);

        r = request_mem_region(ioc->bases, ioc->base_size, MYNAM);
        if (!r) {
                printk(KERN_ERR MYNAM ": ERROR - reserved base 1 failed\n");
                kfree(ioc);
                return res;
        }
        ioc->mmr_base = ioremap(ioc->bases, ioc->base_size);
        ioc->R.CCR.address  = ioc->mmr_base + OFS_CCR;
        ioc->R.CSR.address  = ioc->mmr_base + OFS_CSR;
        ioc->R.DAR.address  = ioc->mmr_base + OFS_DAR;
        ioc->R.NDAR.address = ioc->mmr_base + OFS_NDAR;

        res = request_irq(pdev->irq, lzf_intr_handler, SA_SHIRQ, MYNAM, ioc);
        if (res) {
                printk(KERN_ERR MYNAM ": ERROR - reserved irq %d failed\n",
                                ioc->irq);
                kfree(ioc);
                return res;
        }
        pci_set_drvdata(pdev, ioc);

        for (i = 0; i < MAX_QUEUE; i++) {
                job_entry_t *j;
                j = new_job_entry(ioc);
                list_add_tail(&j->entry, &ioc->free_head);
        }
        init_waitqueue_head(&ioc->wait);
        atomic_set(&ioc->queue, 0);

        start_null_desc(ioc);

        return res;
}

static void __devexit lzf_remove(struct pci_dev *pdev)
{
        /* TODO */
}

static void lzf_shutdown(struct device *dev)
{
        /* TODO */
}

static struct pci_device_id lzf_pci_table[] = {
        { 0x0100, 0x0003, PCI_ANY_ID, PCI_ANY_ID},
        { 0 },
};

static struct pci_driver lzf_driver = {
        .name      = "lzf",
        .id_table  = lzf_pci_table,
        .probe     = lzf_probe,
        .remove    = __devexit_p(lzf_remove),
        .driver    = {
                .shutdown = lzf_shutdown,
        },
};
        
static int __init lzf_init(void)
{
        _cache = kmem_cache_create("lzf_cache", 
                        sizeof(job_desc_t),
                        8, /* for HW */
                        0,
                        NULL,
                        NULL);
        job_cache = kmem_cache_create("job_cache",
                        sizeof(job_entry_t),
                        0, 0, NULL, NULL);
        BUG_ON(sizeof(job_desc_t) != 64);
        BUG_ON(sizeof(res_desc_t) != 64);
        BUG_ON(sizeof(buf_desc_t) != 64);
        return pci_module_init(&lzf_driver);
}

static void __exit lzf_exit(void)
{
        kmem_cache_destroy(_cache);
        kmem_cache_destroy(job_cache);
        pci_unregister_driver(&lzf_driver);
}

module_init(lzf_init);
module_exit(lzf_exit);
MODULE_LICENSE("GPL");
