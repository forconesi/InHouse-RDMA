#ifndef PAGE_SHIFT
#define PAGE_SHIFT	12
#endif
#define LBUF_ORDER	9
#define LBUF_SIZE	(1UL << (PAGE_SHIFT + LBUF_ORDER))

#define NR_RESERVED_DWORDS		32
/* 1st dword is # of qwords, so # of dwords includes it plus reserved area */
#define LBUF_NR_DWORDS(buf_addr)	((((unsigned int *)buf_addr)[0] << 1) + NR_RESERVED_DWORDS)
#define LBUF_FIRST_DWORD_IDX()		NR_RESERVED_DWORDS

/* in each packet, 1st dword  = reserved for timestamp,
 *		   2nd dword  = packet length in bytes
 *		   3rd dword~ = packet payload
 *		   pad = keeping qword-aligned
 */
#define LBUF_TIMESTAMP(buf_addr, dword_idx)	((unsigned int *)buf_addr)[dword_idx]
#define LBUF_PKT_LEN(buf_addr, dword_idx)	((unsigned int *)buf_addr)[dword_idx+1]
#define LBUF_PKT_ADDR(buf_addr, dword_idx)	&((unsigned int *)buf_addr)[dword_idx+2]
#define LBUF_NEXT_DWORD_IDX(buf_addr, dword_idx)	\
	(dword_idx + 2 + (((LBUF_PKT_LEN(buf_addr, dword_idx) + 7) & ~7) >> 2))

/* check functions */
#define LBUF_IS_VALID(nr_dwords)		(nr_dwords > NR_RESERVED_DWORDS && nr_dwords <= (LBUF_SIZE >> 2))
