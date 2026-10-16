#include <math.h>
#include "pcaplite.h"

#define MAGIC_NG 0x0A0D0D0A
#define MACIG 0xA1B2C3D4  

#define MAX_CAN_PACKET_SIZE 256 // CAN FD (72 bytes) and LIN (16) fit; longer packets are cut to this

// some pcap format constants
#define PCAP_FILE_HEADER_LENGTH 24
#define PCAP_FRAME_HEADER_LENGTH 16
#define PCAP_CAP_FRAME_LENGTH_OFFSET 8
#define PCAP_FRAME_LENGTH_OFFSET 12

// some pcapng format constants
#define INTERFACE_DESCRITION_BLOCK 0x01
#define ENCHANCED_PACKET_BLOCK 0x06

static unsigned char pcap_buffer[MAX_CAN_PACKET_SIZE];
static pcap_t p;

// reads a packet of caplen bytes into pcap_buffer, cutting it to the buffer's size (the rest is skipped);
// returns how many bytes are in the buffer, or -1 at the end of the file
static int read_packet(FILE *file, unsigned int caplen)
{
    const unsigned int keep = caplen < sizeof(pcap_buffer) ? caplen : (unsigned int) sizeof(pcap_buffer);
    if (fread(pcap_buffer, 1, keep, file) != keep) return -1;
    if (caplen > keep && fseek(file, (long) (caplen - keep), SEEK_CUR)) return -1;
    return (int) keep;
}

// pcapng: the link type of each interface description block in the section, in order (the file stays where it was)
static void read_interfaces(pcap_t *pc)
{
    pc->interface_count = 0;
    const long start = ftell(pc->file);
    unsigned int header[2]; // block type, block length
    while (fread(header, 1, sizeof(header), pc->file) == sizeof(header)) {
        if (header[1] < 12 || header[0] == MAGIC_NG) break; // damaged, or a new section
        const long block = ftell(pc->file) - (long) sizeof(header);
        if (header[0] == INTERFACE_DESCRITION_BLOCK && pc->interface_count < PCAP_MAX_INTERFACES) {
            const int index = pc->interface_count++;
            unsigned short type = 0;
            if (fread(&type, 1, sizeof(type), pc->file) != sizeof(type)) break;
            pc->interface_types[index] = type;
            pc->interface_names[index][0] = 0;
            // options after the link type, reserved and snapshot length (16 bytes into the block): if_name (2)
            fseek(pc->file, block + 16, SEEK_SET);
            unsigned short option[2]; // code, length
            while (ftell(pc->file) + 4 <= block + (long) header[1] - 4
                   && fread(option, 1, sizeof(option), pc->file) == sizeof(option) && option[0] != 0) {
                if (option[0] == 2) {
                    const unsigned int keep = option[1] < 31 ? option[1] : 31;
                    if (fread(pc->interface_names[index], 1, keep, pc->file) != keep) break;
                    pc->interface_names[index][keep] = 0;
                    if (fseek(pc->file, (long) ((option[1] + 3) & ~3) - (long) keep, SEEK_CUR)) break;
                } else if (fseek(pc->file, (option[1] + 3) & ~3, SEEK_CUR)) break;
            }
        }
        if (fseek(pc->file, block + (long) header[1], SEEK_SET)) break;
    }
    clearerr(pc->file);
    fseek(pc->file, start, SEEK_SET);
}

const char *pcap_interface_name(const pcap_t *pc, unsigned int id)
{
    return (int) id < pc->interface_count ? pc->interface_names[id] : "";
}

bool pcap_has_linktype(const pcap_t *pc, int type)
{
    if (!pc->is_ng) return pc->linktype == type;
    for (int i = 0; i < pc->interface_count; i++)
        if (pc->interface_types[i] == type) return true;
    return false;
}

pcap *pcap_open_offline(const char *filename, char *error_text, int expected_link_type) {
	FILE *file;

    snprintf(error_text, PCAP_ERRBUF_SIZE, "OK");

    file = fopen(filename,"rb");

    if (NULL == file) {
        snprintf(error_text, PCAP_ERRBUF_SIZE,
               "Cannot open input file");
        return NULL;
    }

	unsigned int magic;
	size_t bytes_read;

	bytes_read = fread(&magic, 1, sizeof(magic), file);
	if (bytes_read != sizeof(magic)) {
		snprintf(error_text, PCAP_ERRBUF_SIZE, "Cannot read magic word");
        fclose(file);
		return (NULL);
	}

    if (magic != MACIG && magic != MAGIC_NG) {
        snprintf(error_text, PCAP_ERRBUF_SIZE, "Not a supported format %04x", magic);
        fclose(file);
		return (NULL);
    }

    // set the format
    // and seek past file header
    p.file = file;
    p.interface_count = 0;
    p.linktype = -1;
    if (MAGIC_NG == magic) {
        p.is_ng = 1;
        fseek(file, sizeof(magic), SEEK_SET);
        unsigned int  section_length;
        bytes_read = fread(&section_length, 1, sizeof(section_length), file);
        if (bytes_read != sizeof(section_length)) {
		    snprintf(error_text, PCAP_ERRBUF_SIZE, "Cannot read section length");
            fclose(file);
		    return (NULL);
	    }
        fseek(file, section_length, SEEK_SET);
        // the link types are in the interface description blocks (the section header has none)
        read_interfaces(&p);
        if (p.interface_count > 0) p.linktype = p.interface_types[0];
    } else {
        p.is_ng = 0;
        unsigned int link_type;
        fseek(file, PCAP_FILE_HEADER_LENGTH - 4, SEEK_SET);
        bytes_read = fread(&link_type, 1, sizeof(link_type), file);
        if (bytes_read != sizeof(link_type)) {
            snprintf(error_text, PCAP_ERRBUF_SIZE, "Cannot read linktype word");
            fclose(file);
            return (NULL);
        }
        p.linktype = (int) (link_type & 0xFFFF); // the upper bits can hold FCS details
        fseek(file, PCAP_FILE_HEADER_LENGTH, SEEK_SET);
    }

    if (expected_link_type >= 0 && !pcap_has_linktype(&p, expected_link_type)) {
        snprintf(error_text, PCAP_ERRBUF_SIZE, "This link type is not supported by this decoder");
        fclose(file);
        return (NULL);
    }

	return(&p);
}

const unsigned char *pcap_next_ng(pcap_t *p, struct pcap_pkthdr *h) {
    // this is for ENCHANCED_PACKET_BLOCK
    struct block_header { 
        unsigned int block_type;
        unsigned int block_size;
        unsigned int interface_id;
        unsigned int timestamp_hi;
        unsigned int timestamp_lo;
        unsigned int cap_len;
        unsigned int len;
    } bh;

    struct option_header {
        unsigned short option_type;
        unsigned short option_length;
    } oh;

    size_t bytes_read;

    static unsigned long long timestamp_units = 1000000; //ticks a second (if_tsresol): microseconds by default

    long fpos = ftell(p->file);
    
    do {
        bytes_read = fread(&bh, 1, sizeof(bh), p->file);
        if (bytes_read != sizeof(bh)) {
            //probably EOF
            return (NULL);
        }

        if (bh.block_type != ENCHANCED_PACKET_BLOCK) {
            if (INTERFACE_DESCRITION_BLOCK == bh.block_type) {
                // Seek to start of options part in the header
                fseek(p->file, fpos + 16, SEEK_SET);
                do {
                    bytes_read = fread(&oh, 1, sizeof(oh), p->file);
                    if (bytes_read != sizeof(oh)) {
                        //probably EOF
                        return (NULL);
                    }
                    
                    // calculate option length aligned on 4 byte boundary
                    unsigned int option_file_length = 4*(oh.option_length/4) + ((oh.option_length%4) ? 4:0);

                    
                    // read in the buffer if it's large enough or skip
                    if (option_file_length <= sizeof(pcap_buffer)) {
                        bytes_read = fread(pcap_buffer, 1, option_file_length, p->file);
                        if (bytes_read != option_file_length) {
                            //probably EOF
		                    return (NULL);
	                    }
                        //check if time resolution option
                        if (0x09 == oh.option_type) {
                            // one byte: a power of 10, or of 2 with bit 7 set
                            const unsigned int res = pcap_buffer[0];
                            if ((0x80 & res) == 0) {
                                timestamp_units = 1; for (unsigned int i = 0; i < res && i < 19; i++) timestamp_units *= 10;
                            } else {
                               timestamp_units = 1ULL << ((res & 0x7f) < 63 ? (res & 0x7f) : 63);
                            }
                        }
                    } else if (fseek(p->file, option_file_length, SEEK_CUR)) {
                        //probably EOF
		                return (NULL);
                    }

                } while(ftell(p->file) < fpos + bh.block_size - sizeof(bh.block_size));

                if (fseek(p->file, sizeof(bh.block_size), SEEK_CUR)) {
                        //probably EOF
		                return (NULL);
                }
            }

            // skip to the next block
            if (fseek(p->file, fpos + bh.block_size, SEEK_SET)) {
                //probably EOF
		        return (NULL);
            }
            fpos = ftell(p->file);
        }
    } while (bh.block_type != ENCHANCED_PACKET_BLOCK);

    h->len = bh.len;
    h->interface_id = bh.interface_id;
    h->linktype = (int) bh.interface_id < p->interface_count ? p->interface_types[bh.interface_id] : -1;

    // whole ticks, so microseconds come out exact (a double turned 3500 us into 3499)
    const unsigned long long ticks = (unsigned long long)bh.timestamp_hi << 32 | bh.timestamp_lo;
    h->ts.tv_sec = (long)(ticks / timestamp_units);
    h->ts.tv_usec = (long)((ticks % timestamp_units) * 1000000ULL / timestamp_units);

    const int kept = read_packet(p->file, bh.cap_len);
    if (kept < 0) {
        //probably EOF
		return (NULL);
	}
    h->caplen = (unsigned int) kept;

    // options after the packet data (padded to 4 bytes): epb_flags (2) holds the direction
    h->flags = 0;
    if (!fseek(p->file, fpos + (long) sizeof(bh) + (long) ((bh.cap_len + 3) & ~3u), SEEK_SET)) {
        while (ftell(p->file) + (long) sizeof(oh) <= fpos + (long) bh.block_size - 4
               && fread(&oh, 1, sizeof(oh), p->file) == sizeof(oh) && oh.option_type != 0) {
            if (oh.option_type == 2 && oh.option_length == 4) {
                if (fread(&h->flags, 1, sizeof(h->flags), p->file) != sizeof(h->flags)) break;
            } else if (fseek(p->file, (oh.option_length + 3) & ~3, SEEK_CUR)) break;
        }
        clearerr(p->file);
    }

    //seek to the next block (go back with bytes read of the block and seek fwd with the block size)
    if (fseek(p->file, fpos + bh.block_size, SEEK_SET)) {
        //probably EOF
		return (NULL);
    }

    return pcap_buffer;
}


const unsigned char *pcap_next(pcap_t *p, struct pcap_pkthdr *h)
{
    if (p->is_ng) {
        return pcap_next_ng(p, h);
    }

	size_t bytes_read;

	bytes_read = fread(pcap_buffer, 1, PCAP_FRAME_HEADER_LENGTH, p->file);
    if (bytes_read != PCAP_FRAME_HEADER_LENGTH) {
        //probably EOF
		return (NULL);
	}

    h->ts.tv_sec = *(unsigned int*)(pcap_buffer);
    h->ts.tv_usec = *(unsigned int*)(pcap_buffer + 4);

    const unsigned int caplen = *(unsigned int*)(pcap_buffer + PCAP_CAP_FRAME_LENGTH_OFFSET);
    h->len = *(unsigned int*)(pcap_buffer + PCAP_FRAME_LENGTH_OFFSET);
    h->linktype = p->linktype;
    h->interface_id = 0;
    h->flags = 0;

    const int kept = read_packet(p->file, caplen);
    if (kept < 0) {
        //probably EOF
		return (NULL);
	}
    h->caplen = (unsigned int) kept;

    return pcap_buffer;
}

void pcap_close(pcap_t *p) {
    fclose(p->file);
}
