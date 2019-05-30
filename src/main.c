/*
 *  btslides
 *
 *
 *  EPoX Bluetooth Presenter software (BT-PM01B)
 *
 *  Copyright (C) 2003  Marcel Holtmann <marcel@holtmann.org>
 *
 *  Generic Bluetooth Headset Support
 *
 *  Copyright (C) 2005  Collin Mulliner <collin@betaversion.net>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <sys/poll.h>
#include <sys/socket.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/rfcomm.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sco.h>
#include <bluetooth/sdp_lib.h>

#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>

#include <math.h>

#define EPOX_BUF 16
#define HEADSET_BUF 1024

static int connect_sco(bdaddr_t *src, bdaddr_t *dst)
{
	struct sockaddr_sco addr;
	int s;
	
	if ((s = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_SCO)) < 0) {
		return(-1);
	}

	memset(&addr, 0, sizeof(addr));
	addr.sco_family = AF_BLUETOOTH;
	bacpy(&addr.sco_bdaddr, src);
	if (bind(s, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
		close(s);
		return(-1);
	}

	memset(&addr, 0, sizeof(addr));
	addr.sco_family = AF_BLUETOOTH;
	bacpy(&addr.sco_bdaddr, dst);
	if (connect(s, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
		close(s);
		return(-1);
	}
	
	return(s);
}

static void send_key_event(Display *display, int key)
{
	XTestFakeKeyEvent(display, key, 1, 0);	// press
	XTestFakeKeyEvent(display, key, 0, 0);	// release
	XFlush(display);
}

static void func(Display *display)
{
//	send_key_event(display, up_key);
}

static void back(Display *display)
{
	send_key_event(display, 112);
}

static void next(Display *display)
{
	send_key_event(display, 117);
}

static void button(Display *display, unsigned int button, Bool is_press)
{
	XTestGrabControl(display, True);
	XTestFakeButtonEvent(display, button, is_press, CurrentTime);
	XTestGrabControl(display, False);
	XFlush(display);
}

static void move(Display *display, unsigned int direction)
{
	double angle;
	int x, y;

	angle = (direction * 22.5) * 3.1415926 / 180;
	x = (int) (sin(angle) * 12);
	y = (int) (cos(angle) * -12);

	XTestGrabControl(display, True);
	XTestFakeRelativeMotionEvent(display, x, y, CurrentTime);
	XTestGrabControl(display, False);
	XFlush(display);
}

static void decode_event(Display *display, unsigned char event)
{
	switch (event) {
	case 48:
		func(display); break;
	case 55:
		back(display); break;
	case 56:
		next(display); break;
	case 53:
		button(display, 1, True); break;
	case 121:
		button(display, 1, False); break;
	case 113:
		break;
	case 54:
		button(display, 3, True); break;
	case 120:
		button(display, 3, False); break;
	case 112:
		break;
	case 51:
		move(display, 0); break;
	case 97:
		move(display, 1); break;
	case 65:
		move(display, 2); break;
	case 98:
		move(display, 3); break;
	case 50:
		move(display, 4); break;
	case 99:
		move(display, 5); break;
	case 67:
		move(display, 6); break;
	case 101:
		move(display, 7); break;
	case 52:
		move(display, 8); break;
	case 100:
		move(display, 9); break;
	case 66:
		move(display, 10); break;
	case 102:
		move(display, 11); break;
	case 49:
		move(display, 12); break;
	case 103:
		move(display, 13); break;
	case 57:
		move(display, 14); break;
	case 104:
		move(display, 15); break;
	case 69:
		break;
	default:
		printf("Unknown event code %d\n", event);
		break;
	}
}


static int get_channel(bdaddr_t *src, bdaddr_t *dst, uint8_t *channel)
{
	sdp_session_t *s;
	sdp_list_t *srch, *attrs, *rsp;
	uuid_t svclass;
	uint16_t attr;
	int err;

	if (!(s = sdp_connect(src, dst, 0)))
		return -1;

	sdp_uuid16_create(&svclass, SERIAL_PORT_SVCLASS_ID);
	srch = sdp_list_append(NULL, &svclass);

	attr = SDP_ATTR_PROTO_DESC_LIST;
	attrs = sdp_list_append(NULL, &attr);

	err = sdp_service_search_attr_req(s, srch, SDP_ATTR_REQ_INDIVIDUAL, attrs, &rsp);

	sdp_close(s);

	if (err)
		return 0;

	for (; rsp; rsp = rsp->next) {
		sdp_record_t *rec = (sdp_record_t *) rsp->data;
		sdp_list_t *protos;

		if (!sdp_get_access_protos(rec, &protos)) {
			uint8_t ch = sdp_get_proto_port(protos, RFCOMM_UUID);
			if (ch > 0) {
				*channel = ch;
				return 1;
			}
		}
	}

	return 0;
}

static int search_device(bdaddr_t *src, bdaddr_t *dst)
{
	inquiry_info *info = NULL;
	char bda[18];
	int i, dev_id, num_rsp, length, flags;
	uint8_t class[3];

	ba2str(src, bda);
	dev_id = hci_devid(bda);
	if (dev_id < 0) {
		dev_id = hci_get_route(NULL);
		hci_devba(dev_id, src);
	}

	length  = 8;	/* ~10 seconds */
	num_rsp = 0;
	flags   = IREQ_CACHE_FLUSH;

	printf("Searching ...\n");

	num_rsp = hci_inquiry(dev_id, length, num_rsp, NULL, &info, flags);

	for (i = 0; i < num_rsp; i++) {
		memcpy(class, (info+i)->dev_class, 3);
		if (class[0] == 0x00 && class[1] == 0x40 && class[2] == 0x00) {
			bacpy(dst, &(info+i)->bdaddr);
			ba2str(dst, bda);

			free(info);
			return 1;
		}
	}

	free(info);
	printf("No devices in range or visible\n");

	return 0;
}


static volatile sig_atomic_t __io_canceled = 0;

static void sig_hup(int sig)
{
	return;
}

static void sig_term(int sig)
{
	__io_canceled = 1;
}

static void usage(void)
{
	printf("btslides aka (EPoX Bluetooth Presenter) software version %s\n\n", VERSION);

	printf("Usage:\n"
		"\tbtslides [options] [bdaddr]\n"
		"\n");

	printf("Options:\n"
		"\t-i [hciX|bdaddr]   Local HCI device or BD Address\n"
		"\t-H --headset-mode  Use generic headset\n"
		"\t-u [scancode]      Scancode to send for volume up\n"
		"\t-d [scancode]      Scancode to send for volume down\n"
		"\t-p [scancode]      Scancode to send for pickup\n"
		"\t-h, --help         Display help\n"
		"\n");
}

static struct option main_options[] = {
	{ "help",	0, 0, 'h' },
	{ "device",	1, 0, 'i' },
	{ "headset-mode", 0, 0, 'H' },
	{ "up", 1, 0, 'u' },
	{ "down", 1, 0, 'd' },
	{ "pickup", 1, 0, 'p' },
	{ 0, 0, 0, 0 }
};

int main(int argc, char *argv[])
{
	Display *display;
	int eventbase, errorbase, majorver, minorver;
	char buf[1024] = {0};
	struct sigaction sa;
	struct pollfd p;
	struct sockaddr_rc addr;
	char bda[18];
	bdaddr_t src, dst;
	uint8_t channel = 0;
	int i, opt, sk, len;
	int sco = -1;
	int headset = 0;
	int up_key = 102;
	int down_key = 100;
	int pickup_key = 36;
	int volume[5] = {0};
	int dummy;

	bacpy(&src, BDADDR_ANY);

	while ((opt = getopt_long(argc, argv, "+i:hHu:d:p:", main_options, NULL)) != -1) {
		switch(opt) {
		case 'i':
			if (!strncmp(optarg, "hci", 3))
				hci_devba(atoi(optarg + 3), &src);
			else
				str2ba(optarg, &src);
			break;
		case 'H':
			headset = 1;
			break;
		case 'u':
			up_key = atoi(optarg);
			break;
		case 'd':
			down_key = atoi(optarg);
			break;
		case 'p':
			pickup_key = atoi(optarg);
			break;
		case 'h':
			usage();
			exit(0);
		default:
			exit(0);
		}
	}

	argc -= optind;
	argv += optind;
	optind = 0;

	if (argc < 1) {
		if (!search_device(&src, &dst))
			exit(0);
	} else
		str2ba(argv[0], &dst);

	if (argc < 2) {
		if (!get_channel(&src, &dst, &channel))
			channel = 1;
	} else
		channel = atoi(argv[1]);

	if ((display = XOpenDisplay(XDisplayName(NULL))) == NULL) {
		perror("Can't connect to X display");
		exit(1);
	}

	if (!XTestQueryExtension(display, &eventbase, &errorbase, &majorver, &minorver)) {
		perror("Can't find XTest support");
		exit(1);
	}

	if ((sk = socket(PF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM)) < 0) {
		perror("Can't create socket");
		exit(1);
	}

	addr.rc_family = AF_BLUETOOTH;
	bacpy(&addr.rc_bdaddr, &src);
	addr.rc_channel = 0;

	if (bind(sk, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("Can't bind socket");
		close(sk);
		exit(1);
	}

	addr.rc_family = AF_BLUETOOTH;
	bacpy(&addr.rc_bdaddr, &dst);
	addr.rc_channel = channel;

	if (connect(sk, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("Can't connect socket");
		close(sk);
		exit(1);
	}

	ba2str(&dst, bda);
	printf("Connected to %s on channel %d\n", bda, channel);
	printf("Press CTRL-C for hangup\n");

	memset(&sa, 0, sizeof(sa));
	sa.sa_flags   = SA_NOCLDSTOP;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, NULL);
	sigaction(SIGPIPE, &sa, NULL);

	sa.sa_handler = sig_term;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT,  &sa, NULL);

	sa.sa_handler = sig_hup;
	sigaction(SIGHUP, &sa, NULL);

	p.fd = sk;
	p.events = POLLIN | POLLERR | POLLHUP;

	if (headset) {
		// min volume
		volume[0] = 4;
		// max volume
		volume[1] = 11;
		// adjust threshold
		volume[2] = 2;
		
		/*
		 *  we need to connect the SCO channel to get headset to report volume
		 */
		if ((sco = connect_sco(&src, &dst)) == -1) {
			printf("Can't connect SCO!\n");
			goto disconnected;
		}
		
		while (!__io_canceled) {
			p.revents = 0;
			if (poll(&p, 1, 100) < 1)
				continue;

			bzero(buf, sizeof(buf));
			len = read(sk, buf, HEADSET_BUF);
			if (len < 0)
				break;

			if (0) printf("%s\n", buf);
			
			// ack line
			write(sk, "\r\nOK\r\n", 6);
			
			// volume keys
			if (sscanf(buf, "AT+VGS=%d", &volume[3]) == 1) {
				if (0) printf("o=%d n=%d\n", volume[4], volume[3]);
				if (volume[4] > 0) {
					
					if (volume[4] < volume[3]) {
						send_key_event(display, up_key);
					}
					else if (volume[4] > volume[3]) {
						send_key_event(display, down_key);
					}
					else {
						if (0) printf("volume key press but no volume change?\n");
					}
					
					if (volume[3] < volume[0]+volume[2] || volume[3] > volume[1]-volume[2]) {
						/*
						 *  adjust volume setting
						 *   -this is needed because when the max/min volume is reached the
						 *    headset will not send any volume events
						 *   -we just reset the volume to the middel setting, so there is
						 *    enough space to navigate forward and backward
						 */
 						volume[3] = volume[0]+volume[2]+1;
			 			bzero(buf, sizeof(buf));
						sprintf(buf, "AT+VGS=%d\r\n", volume[3]);
						write(sk, buf, strlen(buf));
					}
				}
				volume[4] = volume[3];
			}
			// pickup key
			else if (sscanf(buf, "AT+CKPD=%d", &dummy) == 1) {
				if (0) printf("pickup key\n");
				send_key_event(display, pickup_key);
			}
		}
		
		if (sco != -1) close(sco);
	}
	// EPOX-presenter
	else {
		while (!__io_canceled) {
			p.revents = 0;
			if (poll(&p, 1, 100) < 1)
				continue;

			len = read(sk, buf, EPOX_BUF);
			if (len < 0)
				break;

			for (i = 0; i < len; i++)
				decode_event(display, buf[i]);
		}
	}

	printf("Disconnected\n");

disconnected:
	close(sk);

	return 0;
}
