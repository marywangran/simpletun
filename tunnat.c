#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <linux/ip.h>
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
 * read_n: ensures we read exactly n bytes, and puts them into "buf".     *
 *         (unless EOF, of course)                                        *
 **************************************************************************/
int read_n(int fd, char *buf, int n)
{
	int nread, left = n;

	while (left > 0) {
		if ((nread = cread(fd, buf, left)) == 0) {
			return 0;
		} else {
			left -= nread;
			buf += nread;
		}
	}
	return n;
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
	uint16_t nread, nwrite ;
	char buffer[BUFSIZE];
	char from_ip[16] = "", to_ip[16] = "";

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
			/* data from tun/tap: just read it and write it to the network */
			unsigned int addr1 = inet_addr(from_ip);
			unsigned int addr2 = inet_addr(to_ip);

			nread = cread(tap_fd, buffer, BUFSIZE);
			iph = (struct iphdr *)buffer;
			if (addr2 == iph->daddr) {
				iph->daddr = addr1;
			} else if (addr1 == iph->saddr) {
				iph->saddr = addr2;
			} else {
				continue;
			}
			iph->check = 0;
			iph->check = ip_checksum((unsigned short *)iph, 20);

			nwrite = cwrite(tap_fd, buffer, nread);
		}
	}
	return(0);
}
