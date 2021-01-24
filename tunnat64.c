#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/icmp.h>
#include <linux/icmpv6.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include <stdarg.h>

/* buffer for reading from tun/tap interface, must be >= 1500 */
#define BUFSIZE 2000
#define NEXTHDR_ICMP		58

struct pseudohdr {
	unsigned char saddr[16];
	unsigned char daddr[16];
	int len;
	unsigned char proto[4];
};

int debug;
char *progname;

static int tun_ifup(const char *ifname)
{
		struct ifreq ifr;
	int sk, err;
	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return -errno;
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(sk, SIOCGIFFLAGS, &ifr) < 0) {
		err = -errno;
		goto done;
	}
	if (ifr.ifr_flags & IFF_UP) {
		err = -EALREADY;
		goto done;
	}
	ifr.ifr_flags |= IFF_UP;
	if (ioctl(sk, SIOCSIFFLAGS, &ifr) < 0) {
		err = -errno;
		goto done;
	}
	err = 0;
done:
	close(sk);
	return err;
}

/**************************************************************************
 * tun_alloc: allocates or reconnects to a tun/tap device. The caller     *
 *            must reserve enough space in *dev.                          *
 **************************************************************************/
int tun_alloc(char *dev, int flags)
{

	struct ifreq ifr;
	int fd, err;
	char *clonedev = "/dev/net/tun";

	if((fd = open(clonedev, O_RDWR)) < 0) {
		perror("Opening /dev/net/tun");
		return fd;
	}

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = flags;

	if (*dev) {
		strncpy(ifr.ifr_name, dev, IFNAMSIZ);
	}

	if((err = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0) {
		perror("ioctl(TUNSETIFF)");
		close(fd);
		return err;
	}

	strcpy(dev, ifr.ifr_name);

	if (err = tun_ifup(ifr.ifr_name)) {
		perror("ifup");
		return err;
	}

	return fd;
}

/**************************************************************************
 * cread: read routine that checks for errors and exits if an error is    *
 *        returned.                                                       *
 **************************************************************************/
int cread(int fd, char *buf, int n)
{
	int nread;

	if((nread=read(fd, buf, n)) < 0) {
		perror("Reading data");
		exit(1);
	}
	return nread;
}

/**************************************************************************
 * cwrite: write routine that checks for errors and exits if an error is  *
 *         returned.                                                      *
 **************************************************************************/
int cwrite(int fd, char *buf, int n)
{
	int nwrite;

	if((nwrite=write(fd, buf, n)) < 0) {
		perror("Writing data");
		exit(1);
	}
	return nwrite;
}

/**************************************************************************
 * do_debug: prints debugging stuff (doh!)                                *
 **************************************************************************/
void do_debug(char *msg, ...)
{
	va_list argp;

	if (debug) {
		va_start(argp, msg);
		vfprintf(stderr, msg, argp);
		va_end(argp);
	}
}

/**************************************************************************
 * my_err: prints custom error messages on stderr.                        *
 **************************************************************************/
void my_err(char *msg, ...)
{
	va_list argp;

	va_start(argp, msg);
	vfprintf(stderr, msg, argp);
	va_end(argp);
}

/**************************************************************************
 * usage: prints usage and exits.                                         *
 **************************************************************************/
void usage(void)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "%s -i <ifacename> [-s|-c <serverIP>] [-p <port>] [-u|-a] [-d]\n", progname);
	fprintf(stderr, "%s -h\n", progname);
	fprintf(stderr, "\n");
	fprintf(stderr, "-i <ifacename>: Name of interface to use (mandatory)\n");
	fprintf(stderr, "-s|-c <serverIP>: run in server mode (-s), or specify server address (-c <serverIP>) (mandatory)\n");
	fprintf(stderr, "-p <port>: port to listen on (if run in server mode) or to connect to (in client mode), default 55555\n");
	fprintf(stderr, "-u|-a: use TUN (-u, default) or TAP (-a)\n");
	fprintf(stderr, "-d: outputs debug information while running\n");
	fprintf(stderr, "-h: prints this help text\n");
	exit(1);
}


unsigned short ip_checksum(unsigned short *buf, unsigned int len)
{
	unsigned long csum = 0;
	while (len > 1) {
		csum += *buf++;
		len -= sizeof(unsigned short);
	}
	if (len) {
		csum += *(unsigned char *)buf;
	}
	csum = (csum >> 16) + (csum & 0xffff);
	csum += (csum >> 16);
	return (unsigned short)(~csum);
}

int main(int argc, char *argv[])
{
	int tap_fd, option;
	int flags = IFF_TUN;
	char if_name[IFNAMSIZ] = "";
	int maxfd;
	unsigned short ipid = 1234;
	uint16_t nread, nwrite ;
	char buffer[BUFSIZE], buffer6[BUFSIZE], buf[BUFSIZE];
	char from_ip[16] = "", to_ip[16] = "";
	char ip6saddr[16], ip6daddr[16];
	struct ipv6hdr stored_hdr = {0};

	progname = argv[0];

	while((option = getopt(argc, argv, "i:xo:m:uahd")) > 0) {
		switch(option) {
			case 'd':
				debug = 1;
				break;
			case 'h':
				usage();
				break;
			case 'i':
				strncpy(if_name,optarg, IFNAMSIZ-1);
				break;
			case 'o':
				strncpy(from_ip,optarg,15);
				break;
			case 'm':
				strncpy(to_ip,optarg,15);
				break;
			default:
				my_err("Unknown option %c\n", option);
				usage();
		}
	}

	argv += optind;
	argc -= optind;

	if (argc > 0) {
		my_err("Too many options!\n");
		usage();
	}

	if (*if_name == '\0') {
		my_err("Must specify interface name!\n");
		usage();
	}

	if ((tap_fd = tun_alloc(if_name, IFF_TUN | IFF_NO_PI)) < 0) {
		my_err("Error connecting to tun/tap interface %s!\n", if_name);
		exit(1);
	}

	do_debug("Successfully connected to interface %s\n", if_name);

	/* use select() to handle two descriptors at once */
	maxfd = tap_fd;

	while (1) {
		int ret;
		unsigned int len = 0;
		fd_set rd_set;

		FD_ZERO(&rd_set);
		FD_SET(tap_fd, &rd_set);

		ret = select(maxfd + 1, &rd_set, NULL, NULL, NULL);

		if (ret < 0 && errno == EINTR) {
			continue;
		}

		if (ret < 0) {
			perror("select()");
			exit(1);
		}

		if (FD_ISSET(tap_fd, &rd_set)) {
			struct iphdr *iph;
			struct ipv6hdr *ip6h;
			struct icmphdr	*icmph;
			unsigned int dstip;
			/* data from tun/tap: just read it and write it to the network */
			unsigned short payloadlen;
			unsigned short type;
			unsigned int addr2 = inet_addr(to_ip);

			nread = cread(tap_fd, buffer, BUFSIZE);
			iph = (struct iphdr *)buffer;
			ip6h = (struct ipv6hdr *)buffer;
			if (addr2 == iph->daddr) {
				memcpy(buffer6, &stored_hdr, sizeof(struct ipv6hdr));
				memcpy(buffer6 + sizeof(struct ipv6hdr), buffer + sizeof(struct iphdr), nread - sizeof(struct iphdr));
				ip6h = (struct ipv6hdr *)buffer6;
				ip6h->payload_len = htons(ntohs(iph->tot_len) - sizeof(struct iphdr));
				ip6h->hop_limit = iph->ttl;
				memcpy(&ip6h->saddr, ip6daddr, 16);
				memcpy(&ip6h->daddr, ip6saddr, 16);
				len = nread - sizeof(struct iphdr) + sizeof(struct ipv6hdr);

				if (ip6h->nexthdr == NEXTHDR_ICMP) {
					struct pseudohdr phdr;

					icmph = (struct icmphdr *)(buffer6 + sizeof(struct ipv6hdr));
					memcpy(&phdr.saddr, &ip6h->saddr, 16);
					memcpy(&phdr.daddr, &ip6h->daddr, 16);
					phdr.len = ip6h->payload_len;
					phdr.proto[3] = NEXTHDR_ICMP;

					type = (ICMPV6_ECHO_REPLY << 8) + icmph->code;
					icmph->type = type >> 8;
					icmph->code = type & 0xFF;
					icmph->checksum = 0;
					memset(buf, 0, BUFSIZE);
					memcpy(buf, icmph, ntohs(ip6h->payload_len));
					memcpy(buf + ntohs(ip6h->payload_len), &phdr, sizeof(phdr));
					icmph->checksum = ip_checksum((unsigned short *)buf, ntohs(ip6h->payload_len) + sizeof(phdr));
				}
				nwrite = cwrite(tap_fd, (char *)buffer6, len);
			} else if (1)/*.....*/{
				memcpy(&stored_hdr, ip6h, sizeof(struct ipv6hdr));
				memcpy(ip6saddr, &ip6h->saddr, 16);
				memcpy(ip6daddr, &ip6h->daddr, 16);
				dstip = *(unsigned int *)((unsigned char *)&ip6h->daddr + 12);
				payloadlen = ntohs(ip6h->payload_len);
				iph = (struct iphdr *)(buffer + sizeof(struct ipv6hdr) - sizeof(struct iphdr));
				iph->tos = 0;
				*((unsigned short *)iph) = htons((4 << 12) | (5 << 8) | (0 & 0xff));
				iph->tot_len = htons(payloadlen + sizeof(struct iphdr));
				iph->id = htons(ipid ++);
				iph->frag_off = 0;
				iph->ttl = ip6h->hop_limit;
				iph->protocol = IPPROTO_ICMP;

				iph->saddr = addr2;
				iph->daddr = dstip;
				len = nread - sizeof(struct ipv6hdr) + sizeof(struct iphdr);
				if (ip6h->nexthdr == NEXTHDR_ICMP) {
					icmph = (struct icmphdr *)((char *)iph + sizeof(struct iphdr));
					type = (ICMP_ECHO << 8) + icmph->code;
					icmph->type = type >> 8;
					icmph->code = type & 0xFF;
					icmph->checksum = 0;
					icmph->checksum = ip_checksum((unsigned short *)icmph, len - sizeof(struct iphdr));
				}
				iph->check = 0;
				iph->check = ip_checksum((unsigned short *)iph, 20);
				nwrite = cwrite(tap_fd, (char *)iph, len);
			} else {
				continue;
			}
		}
	}
	return(0);
}
