#ifndef _LZF_CHIP_H_
#define _LZF_CHIP_H_

/* Data bit */
#define BIT_0	0x1
#define BIT_1	0x2
#define BIT_2	0x4
#define BIT_3	0x8
#define BIT_4	0x10
#define BIT_5	0x20
#define BIT_6	0x40
#define BIT_7	0x80
#define BIT_8	0x100
#define BIT_9	0x200
#define BIT_10	0x400
#define BIT_11	0x800
#define BIT_12	0x1000
#define BIT_13	0x2000
#define BIT_14	0x4000
#define BIT_15	0x8000
#define BIT_16	0x10000
#define BIT_17	0x20000
#define BIT_18	0x40000
#define BIT_19	0x80000
#define BIT_20	0x100000
#define BIT_21	0x200000
#define BIT_22	0x400000
#define BIT_23	0x800000
#define BIT_24	0x1000000
#define BIT_25	0x2000000
#define BIT_26	0x4000000
#define BIT_27	0x8000000
#define BIT_28	0x10000000
#define BIT_29	0x20000000
#define BIT_30	0x40000000
#define BIT_31	0x80000000

enum {
        OFS_CCR = 0x0,
#define CCR_RESUME BIT_0
#define CCR_ENABLE BIT_1
        OFS_CSR = 0x1,
#define CSR_INTP   BIT_0
#define CSR_BUSY   BIT_1
        OFS_DAR = 0x2,
        OFS_NDAR= 0x3,
};

typedef struct {
        uint32_t next_desc; /* 0 [31:03] */
        uint32_t ctl_addr;  /* 1 [31:03] */
        uint32_t dc_fc;     /* 2 [15:00] dc 
                                 [23:16] fc */
        uint32_t u0;        /* 3 */
        uint32_t src_desc;  /* 4 [31:3] */
        uint32_t u1;        /* 5 */
        uint32_t dst_desc;  /* 6 [31:3] */
        uint32_t u2;        /* 7 */
} job_desc_t;

typedef struct {
        uint32_t desc;      /* 0 [11:00] total size 
                                 [20]    LAST */
        uint32_t desc_adr0; /* 1 [31:03] */
        uint32_t desc_adr1; /* 2 [31:03] */
        uint32_t desc_next; /* 3 [31:03] */
} buf_desc_t;

#endif