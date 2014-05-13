#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdint.h>
#include <signal.h>

#include "nf10_lbuf.h"
#include "nf10_fops.h"

#define LBUF_SIZE	(2*1024*1024)
#define DEV_FNAME	"/dev/" NF10_DRV_NAME
#define debug(format, arg...)	\
	do { printf(NF10_DRV_NAME ":" format, ##arg); } while(0)

int fd;
unsigned long total_rx_packets;
void finish(int sig)
{
	uint64_t dummy;
	printf("received packets = %lu\n", total_rx_packets);
	ioctl(fd, NF10_IOCTL_CMD_INIT, &dummy);
	exit(0);
}

int main(int argc, char *argv[])
{
	uint64_t cons;
	uint32_t rx_cons;
	int i;
	void *buf[NR_LBUF];
	uint32_t *lbuf_addr;
	uint32_t nr_qwords;
	int dword_idx, max_dword_idx;
	uint32_t pkt_len;
	uint8_t bytes_remainder;
	uint32_t rx_packets;

	if ((fd = open(DEV_FNAME, O_RDWR, 0755)) < 0) {
		perror("open");
		return -1;
	}

	if (ioctl(fd, NF10_IOCTL_CMD_INIT, &cons)) {
		perror("ioctl init");
		return -1;
	}
	rx_cons = (uint32_t)cons;

	debug("initialized for direct user access (rx_cons=%u)\n", rx_cons);

	for (i = 0; i < NR_LBUF; i++) {
		/* PROT_READ for rx only */
		buf[i] = mmap(NULL, LBUF_SIZE, PROT_READ, MAP_PRIVATE, fd, 0);
		if (buf[i] == MAP_FAILED) {
			perror("mmap");
			return -1;
		}
		debug("lbuf[%d] is mmaped to vaddr=%p w/ size=%u\n",
		      i, buf[i], LBUF_SIZE);
	}

	signal(SIGINT, finish);
	do {

		/* wait interrupt: blocked */
		ioctl(fd, NF10_IOCTL_CMD_WAIT_INTR);

		lbuf_addr = buf[rx_cons];
		nr_qwords = lbuf_addr[0];   /* first 32bit is # of qwords */
		max_dword_idx = (nr_qwords << 1) + 32;
		rx_packets = 0;

		if (nr_qwords == 0 || max_dword_idx > 524288) {
			fprintf(stderr, "rx_cons=%d's header contains invalid # of qwords=%u\n",
					rx_cons, nr_qwords);
			goto next_buf;
		}

		/* dword 1 to 31 are reserved */
		dword_idx = 32;
		do {
			dword_idx++;                    /* reserved for timestamp */
			pkt_len = lbuf_addr[dword_idx++];

			/* FIXME: replace constant */
			if (pkt_len < 60 || pkt_len > 1518) {
				fprintf(stderr, "Error: rx_cons=%d lbuf contains invalid pkt len=%u\n",
					rx_cons, pkt_len);
				goto next_pkt;
			}

			rx_packets++;
next_pkt:
			dword_idx += pkt_len >> 2;      /* byte -> dword */
			bytes_remainder = pkt_len & 0x7;
			if (bytes_remainder >= 4)
				dword_idx++;
			else if (bytes_remainder > 0)
				dword_idx += 2;
		} while(dword_idx < max_dword_idx);
next_buf:
		ioctl(fd, NF10_IOCTL_CMD_PREPARE_RX, rx_cons);
		total_rx_packets += rx_packets;
#if 0
		debug("C [rx_cons=%d] nr_qwords=%u rx_packets=%u/%lu\n",
				rx_cons, nr_qwords, rx_packets, total_rx_packets);
#endif
		rx_cons = rx_cons ? 0 : 1;
	} while(1);

	return 0;
}
