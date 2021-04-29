// gcc midbox.c -lpthread

// with multiqueue feature
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <pthread.h>

#define TUN_PATH "/dev/net/tun"

int *tun_fd_txrx;
int gnum;

struct idx {
	int id;
};

static void tun_setup(int num, int *fds, char *name)
{
	struct ifreq ifr;
	int fd, i;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI | IFF_MULTI_QUEUE;
	strcpy(ifr.ifr_name, name);

	for (i = 0; i < num; i++) {
		fds[i] = open(TUN_PATH, O_RDWR);
		if (fds[i] < 0) {
			exit(1);
		}

		if (ioctl(fds[i], TUNSETIFF, &ifr) < 0) {
			exit(1);
		}
	}
}

void *fwd(void *arg)
{
	struct idx *is = (struct idx *)arg;
	int tx, rx, ret;
	char buf[1500];

	tx = tun_fd_txrx[is->id];
	rx = tun_fd_txrx[is->id + gnum];
	printf ("%d  tx:%d  rx:%d\n", is->id, tx, rx);

	while (1) {
		ret = read(tx, buf, sizeof(buf));
		write(rx, buf, ret);
	}
}

int main(int argc, char *argv[])
{
	int num = atoi(argv[1]);
	int i;
	struct idx *is;

	gnum = num;
	is = (struct idx *)malloc(sizeof(struct idx)*num);
	tun_fd_txrx = (int *)malloc(2*sizeof(int)*num);
	tun_setup(num, &tun_fd_txrx[0], "tun1");
	tun_setup(num, &tun_fd_txrx[num], "tun2");

	for (i = 0; i < num; i++) {
		pthread_t tid;
		is[i].id = i;
		pthread_create(&tid, NULL, fwd, &is[i]);
	}


	getchar();
}
