#ifndef PCAPLITE_H
#define PCAPLITE_H

#include <stdio.h>
#if defined(unix) || defined __APPLE__
#include <sys/time.h>
#else
#include <winsock.h>
#endif

#define PCAP_ERRBUF_SIZE        (256)
#define PCAP_LINKTYPE_SOCKETCAN (227)
#define PCAP_LINKTYPE_LIN       (212)
#define PCAP_LINKTYPE_ANY       (-1)
#define PCAP_MAX_INTERFACES     (64)

struct pcap_pkthdr {
	struct timeval ts;	/* time stamp */
	unsigned int caplen;	/* length of portion present (at most the buffer's size: longer packets are cut) */
	unsigned int len;	/* length of this packet (off wire) */
	int linktype;		/* the packet's link type: the file's, or (pcapng) its interface's; -1 if unknown */
	unsigned int interface_id; /* pcapng: the interface (0 for pcap files) */
	unsigned int flags;	/* pcapng: the packet's epb_flags (bits 0-1: 1 inbound, 2 outbound); 0 if none */
};

struct pcap {
    FILE *file;
    bool is_ng;
    int linktype;       /* pcap: the file's link type; pcapng: the first interface's (-1 if none) */
    int interface_count;
    int interface_types[PCAP_MAX_INTERFACES]; /* pcapng: each interface's link type, in order */
    char interface_names[PCAP_MAX_INTERFACES][32]; /* pcapng: each interface's if_name (cut to 31 characters) */
};

typedef struct pcap pcap_t;

/* opens a pcap or pcapng file; with a link type other than PCAP_LINKTYPE_ANY, only a file with packets of that type
   (a pcap file of that link type, or a pcapng file with an interface of that type) */
pcap *pcap_open_offline(const char *, char *, int);

/* whether the file has packets of this link type */
bool pcap_has_linktype(const pcap_t *, int);

/* pcapng: an interface's name (if_name), empty if it has none */
const char *pcap_interface_name(const pcap_t *, unsigned int);

const unsigned char *pcap_next(pcap_t *, struct pcap_pkthdr *);

void pcap_close(pcap_t *);

#endif// PCAPLITE_H
