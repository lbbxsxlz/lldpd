/*
 * Copyright (c) 2008 Vincent Bernat <bernat@luffy.cx>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "lldpd.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>

void		 usage(void);

TAILQ_HEAD(interfaces, lldpd_interface);
#ifdef ENABLE_DOT1
TAILQ_HEAD(vlans, lldpd_vlan);
#endif

#define ntohll(x) (((u_int64_t)(ntohl((int)((x << 32) >> 32))) << 32) |	\
	    (unsigned int)ntohl(((int)(x >> 32))))
#define htonll(x) ntohll(x)


struct value_string {
	int value;
	char *string;
};

#ifdef ENABLE_LLDPMED
static const struct value_string civic_address_type_values[] = {
        { 0,    "Language" },
        { 1,    "National subdivisions" },
        { 2,    "County, parish, district" },
        { 3,    "City, township" },
        { 4,    "City division, borough, ward" },
        { 5,    "Neighborhood, block" },
        { 6,    "Street" },
        { 16,   "Leading street direction" },
        { 17,   "Trailing street suffix" },
        { 18,   "Street suffix" },
        { 19,   "House number" },
        { 20,   "House number suffix" },
        { 21,   "Landmark or vanity address" },
        { 22,   "Additional location info" },
        { 23,   "Name" },
        { 24,   "Postal/ZIP code" },
        { 25,   "Building" },
        { 26,   "Unit" },
        { 27,   "Floor" },
        { 28,   "Room number" },
        { 29,   "Place type" },
        { 128,  "Script" },
        { 0, NULL }
};
#endif

#ifdef ENABLE_DOT3
static const struct value_string operational_mau_type_values[] = {
	{ 1,	"AUI - no internal MAU, view from AUI" },
	{ 2,	"10Base5 - thick coax MAU" },
	{ 3,	"Foirl - FOIRL MAU" },
	{ 4,	"10Base2 - thin coax MAU" },
	{ 5,	"10BaseT - UTP MAU" },
	{ 6,	"10BaseFP - passive fiber MAU" },
	{ 7,	"10BaseFB - sync fiber MAU" },
	{ 8,	"10BaseFL - async fiber MAU" },
	{ 9,	"10Broad36 - broadband DTE MAU" },
	{ 10,	"10BaseTHD - UTP MAU, half duplex mode" },
	{ 11,	"10BaseTFD - UTP MAU, full duplex mode" },
	{ 12,	"10BaseFLHD - async fiber MAU, half duplex mode" },
	{ 13,	"10BaseFLDF - async fiber MAU, full duplex mode" },
	{ 14,	"10BaseT4 - 4 pair category 3 UTP" },
	{ 15,	"100BaseTXHD - 2 pair category 5 UTP, half duplex mode" },
	{ 16,	"100BaseTXFD - 2 pair category 5 UTP, full duplex mode" },
	{ 17,	"100BaseFXHD - X fiber over PMT, half duplex mode" },
	{ 18,	"100BaseFXFD - X fiber over PMT, full duplex mode" },
	{ 19,	"100BaseT2HD - 2 pair category 3 UTP, half duplex mode" },
	{ 20,	"100BaseT2DF - 2 pair category 3 UTP, full duplex mode" },
	{ 21,	"1000BaseXHD - PCS/PMA, unknown PMD, half duplex mode" },
	{ 22,	"1000BaseXFD - PCS/PMA, unknown PMD, full duplex mode" },
	{ 23,	"1000BaseLXHD - Fiber over long-wavelength laser, half duplex mode" },
	{ 24,	"1000BaseLXFD - Fiber over long-wavelength laser, full duplex mode" },
	{ 25,	"1000BaseSXHD - Fiber over short-wavelength laser, half duplex mode" },
	{ 26,	"1000BaseSXFD - Fiber over short-wavelength laser, full duplex mode" },
	{ 27,	"1000BaseCXHD - Copper over 150-Ohm balanced cable, half duplex mode" },
	{ 28,	"1000BaseCXFD - Copper over 150-Ohm balanced cable, full duplex mode" },
	{ 29,	"1000BaseTHD - Four-pair Category 5 UTP, half duplex mode" },
	{ 30,	"1000BaseTFD - Four-pair Category 5 UTP, full duplex mode" },
	{ 31,	"10GigBaseX - X PCS/PMA, unknown PMD." },
	{ 32,	"10GigBaseLX4 - X fiber over WWDM optics" },
	{ 33,	"10GigBaseR - R PCS/PMA, unknown PMD." },
	{ 34,	"10GigBaseER - R fiber over 1550 nm optics" },
	{ 35,	"10GigBaseLR - R fiber over 1310 nm optics" },
	{ 36,	"10GigBaseSR - R fiber over 850 nm optics" },
	{ 37,	"10GigBaseW - W PCS/PMA, unknown PMD." },
	{ 38,	"10GigBaseEW - W fiber over 1550 nm optics" },
	{ 39,	"10GigBaseLW - W fiber over 1310 nm optics" },
	{ 40,	"10GigBaseSW - W fiber over 850 nm optics" },
	{ 0, NULL }
};
#endif

void
usage(void)
{
	extern const char	*__progname;

	fprintf(stderr, "usage: %s [-d]\n", __progname);
	exit(1);
}

static char*
dump(void *data, int size, int max, char sep)
{
	int			 i;
	size_t			 len;
	static char		*buffer = NULL;
	static char		 truncation[] = "[...]";

	free(buffer);
	if (size > max)
		len = max * 3 + sizeof(truncation) + 1;
	else
		len = size * 3;

	if ((buffer = (char *)malloc(len)) == NULL)
		fatal(NULL);

	for (i = 0; (i < size) && (i < max); i++)
		sprintf(buffer + i * 3, "%02x%c", *(u_int8_t*)(data + i), sep);
	if (size > max)
		sprintf(buffer + i * 3, "%s", truncation);
	else
		*(buffer + i*3 - 1) = 0;
	return buffer;
}


void
get_interfaces(int s, struct interfaces *ifs)
{
	void *p;
	struct hmsg *h;

	if ((h = (struct hmsg *)malloc(MAX_HMSGSIZE)) == NULL)
		fatal(NULL);
	ctl_msg_init(h, HMSG_GET_INTERFACES);
	if (ctl_msg_send(s, h) == -1)
		fatalx("get_interfaces: unable to send request");
	if (ctl_msg_recv(s, h) == -1)
		fatalx("get_interfaces: unable to receive answer");
	if (h->hdr.type != HMSG_GET_INTERFACES)
		fatalx("get_interfaces: unknown answer type received");
	p = &h->data;
	if (ctl_msg_unpack_list(STRUCT_LLDPD_INTERFACE,
		ifs, sizeof(struct lldpd_interface), h, &p) == -1)
		fatalx("get_interfaces: unable to retrieve the list of interfaces");
}

#ifdef ENABLE_DOT1
int
get_vlans(int s, struct vlans *vls, char *interface)
{
	void *p;
	struct hmsg *h;

	if ((h = (struct hmsg *)malloc(MAX_HMSGSIZE)) == NULL)
		fatal(NULL);
	ctl_msg_init(h, HMSG_GET_VLANS);
	h->hdr.len += strlcpy((char *)&h->data, interface,
	    MAX_HMSGSIZE - sizeof(struct hmsg_hdr)) + 1;
	if (ctl_msg_send(s, h) == -1)
		fatalx("get_vlans: unable to send request");
	if (ctl_msg_recv(s, h) == -1)
		fatalx("get_vlans: unable to receive answer");
	if (h->hdr.type != HMSG_GET_VLANS)
		fatalx("get_vlans: unknown answer type received");
	p = &h->data;
	if (ctl_msg_unpack_list(STRUCT_LLDPD_VLAN,
		vls, sizeof(struct lldpd_vlan), h, &p) == -1)
		fatalx("get_vlans: unable to retrieve the list of vlans");
	return 1;
}
#endif

int
get_chassis(int s, struct lldpd_chassis *chassis, char *interface)
{
	struct hmsg *h;
	void *p;

	if ((h = (struct hmsg *)malloc(MAX_HMSGSIZE)) == NULL)
		fatal(NULL);
	ctl_msg_init(h, HMSG_GET_CHASSIS);
	h->hdr.len += strlcpy((char *)&h->data, interface,
	    MAX_HMSGSIZE - sizeof(struct hmsg_hdr)) + 1;
	if (ctl_msg_send(s, h) == -1)
		fatalx("get_chassis: unable to send request to get chassis");
	if (ctl_msg_recv(s, h) == -1)
		fatalx("get_chassis: unable to receive answer to get chassis");
	if (h->hdr.type == HMSG_NONE)
		/* No chassis */
		return -1;
	p = &h->data;
	if (ctl_msg_unpack_structure(STRUCT_LLDPD_CHASSIS,
		chassis, sizeof(struct lldpd_chassis), h, &p) == -1) {
		LLOG_WARNX("unable to retrieve chassis for %s", interface);
		fatalx("get_chassis: abort");
	}
	return 1;
}

int
get_port(int s, struct lldpd_port *port, char *interface)
{
	struct hmsg *h;
	void *p;

	if ((h = (struct hmsg *)malloc(MAX_HMSGSIZE)) == NULL)
		fatal(NULL);
	ctl_msg_init(h, HMSG_GET_PORT);
	h->hdr.len += strlcpy((char *)&h->data, interface,
	    MAX_HMSGSIZE - sizeof(struct hmsg_hdr)) + 1;
	if (ctl_msg_send(s, h) == -1)
		fatalx("get_port: unable to send request to get port");
	if (ctl_msg_recv(s, h) == -1)
		fatalx("get_port: unable to receive answer to get port");
	if (h->hdr.type == HMSG_NONE)
		/* No port */
		return -1;
	p = &h->data;
	if (ctl_msg_unpack_structure(STRUCT_LLDPD_PORT,
		port, sizeof(struct lldpd_port), h, &p) == -1) {
		LLOG_WARNX("unable to retrieve port information for %s",
		    interface);
		fatalx("get_chassis: abort");
	}
	return 1;
}

void
display_cap(struct lldpd_chassis *chassis, u_int8_t bit, char *symbol)
{
	if (chassis->c_cap_available & bit)
		printf("%s(%c) ", symbol,
		    (chassis->c_cap_enabled & bit)?'E':'d');
}

void
pretty_print(char *string)
{
	char *s = NULL;
	if (((s = index(string, '\n')) == NULL) && (strlen(string) < 60)) {
		printf("%s\n", string);
		return;
	} else
		printf("\n");
	while (s != NULL) {
		*s = '\0';
		printf("   %s\n", string);
		*s = '\n';
		string = s + 1;
		s = index(string, '\n');
	}
	printf("   %s\n", string);
}

#ifdef ENABLE_LLDPMED
void
display_latitude_or_longitude(int option, u_int64_t value)
{
	/* Adapted from wireshark lldp dissector */
	u_int64_t tmp = value;
	int negative = 0;
	u_int32_t integer = 0;
	const char *direction;

	if (value & 0x0000000200000000ULL) {
		negative = 1;
		tmp = ~value;
		tmp += 1;
	}
	integer = (u_int32_t)((tmp & 0x00000003FE000000ULL) >> 25);
	tmp = (tmp & 0x0000000001FFFFFFULL)/33554432;
	if (option == 0) {
		if (negative) direction = "South";
		else direction = "North";
	} else {
		if (negative) direction = "West";
		else direction = "East";
	}
	printf("%u.%04llu debrees %s",
	    integer, (unsigned long long int)tmp,
	    direction);
}

void
display_med(struct lldpd_chassis *chassis)
{
	int i;
	char *value;
	printf(" LLDP-MED Device Type: ");
	switch (chassis->c_med_type) {
	case LLDPMED_CLASS_I:
		printf("Generic Endpoint (Class I)");
		break;
	case LLDPMED_CLASS_II:
		printf("Media Endpoint (Class II)");
		break;
	case LLDPMED_CLASS_III:
		printf("Communication Device Endpoint (Class III)");
		break;
	case LLDPMED_NETWORK_DEVICE:
		printf("Network Connectivity Device");
		break;
	default:
		printf("Unknown (%d)", chassis->c_med_type);
		break;
	}
	printf("\n LLDP-MED Capabilities:");
	if (chassis->c_med_cap_available & LLDPMED_CAP_CAP)
		printf(" Capabilities");
	if (chassis->c_med_cap_available & LLDPMED_CAP_POLICY)
		printf(" Policy");
	if (chassis->c_med_cap_available & LLDPMED_CAP_LOCATION)
		printf(" Location");
	if (chassis->c_med_cap_available & LLDPMED_CAP_MDI_PSE)
		printf(" MDI/PSE");
	if (chassis->c_med_cap_available & LLDPMED_CAP_MDI_PD)
		printf(" MDI/PD");
	if (chassis->c_med_cap_available & LLDPMED_CAP_IV)
		printf(" Inventory");
	printf("\n");
	for (i = 0; i < LLDPMED_APPTYPE_LAST; i++) {
		if (i+1 == chassis->c_med_policy[i].type) {
			printf(" LLDP-MED Network Policy for ");
			switch(chassis->c_med_policy[i].type) {
			case LLDPMED_APPTYPE_VOICE:
				printf("Voice");
				break;
			case LLDPMED_APPTYPE_VOICESIGNAL:
				printf("Voice Signaling");
				break;
			case LLDPMED_APPTYPE_GUESTVOICE:
				printf("Guest Voice");
				break;
			case LLDPMED_APPTYPE_GUESTVOICESIGNAL:
				printf("Guest Voice Signaling");
				break;
			case LLDPMED_APPTYPE_SOFTPHONEVOICE:
				printf("Softphone Voice");
				break;
			case LLDPMED_APPTYPE_VIDEOCONFERENCE:
				printf("Video Conferencing");
				break;
			case LLDPMED_APPTYPE_VIDEOSTREAM:
				printf("Streaming Video");
				break;
			case LLDPMED_APPTYPE_VIDEOSIGNAL:
				printf("Video Signaling");
				break;
			default:
				printf("Reserved");
			}
			printf(":\n  Policy:           ");
			if (chassis->c_med_policy[i].unknown) {
				printf("unknown, ");
			} else {
				printf("defined, ");
			}
			if (!chassis->c_med_policy[i].tagged) {
				printf("un");
			}
			printf("tagged");
			printf("\n  VLAN ID:          ");
			if (chassis->c_med_policy[i].vid == 0) {
				printf("Priority Tagged");
			} else if (chassis->c_med_policy[i].vid == 4095) {
				printf("reserved");
			} else {
				printf("%u", chassis->c_med_policy[i].vid);
			}
			printf("\n  Layer 2 Priority: ");
			printf("%u", chassis->c_med_policy[i].priority);
			printf("\n  DSCP Value:       ");
			printf("%u\n", chassis->c_med_policy[i].dscp);
		}
	}
	for (i = 0; i < LLDPMED_LOCFORMAT_LAST; i++) {
		if (i+1 == chassis->c_med_location[i].format) {
			printf(" LLDP-MED Location Identification: ");
			switch(chassis->c_med_location[i].format) {
			case LLDPMED_LOCFORMAT_COORD:
				printf("Coordinate-based data: ");
				if (chassis->c_med_location[i].data_len != 16)
					printf("bad data length");
				else {
					u_int64_t l;
					l = (ntohll(*(u_int64_t*)chassis->c_med_location[i].data) &
					    0x03FFFFFFFF000000ULL) >> 24;
					display_latitude_or_longitude(0, l);
					printf(", ");
					l = (ntohll(*(u_int64_t*)(chassis->c_med_location[i].data + 5)) &
					    0x03FFFFFFFF000000ULL) >> 24;
					display_latitude_or_longitude(1, l);
					/* TODO: altitude */
				}
				break;
			case LLDPMED_LOCFORMAT_CIVIC:
				printf("Civic address: ");
				if ((chassis->c_med_location[i].data_len < 3) ||
				    (chassis->c_med_location[i].data_len - 1 !=
					*(u_int8_t*)chassis->c_med_location[i].data))
					printf("bad data length");
				else {
					int l = 4, n, catype, calength, j = 0;
					printf("\n%28s: %c%c", "Country",
					    ((char *)chassis->c_med_location[i].data)[2],
					    ((char *)chassis->c_med_location[i].data)[3]);
					while ((n = (chassis->
						    c_med_location[i].data_len - l)) >= 2) {
						catype = *(u_int8_t*)(chassis->
						    c_med_location[i].data + l);
						calength = *(u_int8_t*)(chassis->
						    c_med_location[i].data + l + 1);
						if (n < 2 + calength) {
							printf("bad data length");
							break;
						}
						for (j = 0;
						     civic_address_type_values[j].string != NULL;
						     j++) {
							if (civic_address_type_values[j].value ==
							    catype)
								break;
						}
						if (civic_address_type_values[j].string == NULL) {
							printf("unknown type %d", catype);
							break;
						}
						if ((value = strndup((char *)(chassis->
							c_med_location[i].data + l + 2),
							    calength)) == NULL) {
							printf("not enough memory");
							break;
						}
						printf("\n%28s: %s",
						    civic_address_type_values[j].string,
						    value);
						free(value);
						l += 2 + calength;
					}
				}
				break;
			case LLDPMED_LOCFORMAT_ELIN:
				if ((value = strndup((char *)(chassis->
						c_med_location[i].data),
					    chassis->c_med_location[i].data_len)) == NULL) {
					printf("not enough memory");
					break;
				}
				printf("ECS ELIN: %s", value);
				free(value);
				break;
			default:
				printf("unknown location data format: \n   %s",
				    dump(chassis->c_med_location[i].data,
					chassis->c_med_location[i].data_len, 20, ' '));
			}
			printf("\n");
		}
	}
	if (chassis->c_med_pow_devicetype) {
		printf(" LLDP-MED Extended Power-over-Ethernet:\n");
		printf("  Power Type & Source: ");
		switch (chassis->c_med_pow_devicetype) {
		case LLDPMED_POW_TYPE_PSE:
			printf("PSE Device");
			break;
		case LLDPMED_POW_TYPE_PD:
			printf("PD Device");
			break;
		default:
			printf("reserved");
		}
		switch (chassis->c_med_pow_source) {
		case LLDPMED_POW_SOURCE_UNKNOWN:
		case LLDPMED_POW_SOURCE_RESERVED:
			printf(", unknown"); break;
		case LLDPMED_POW_SOURCE_PRIMARY:
			printf(", Primary Power Source");
			break;
		case LLDPMED_POW_SOURCE_BACKUP:
			printf(", Backup Power Source / Power Conservation Mode");
			break;
		case LLDPMED_POW_SOURCE_PSE:
			printf(", PSE"); break;
		case LLDPMED_POW_SOURCE_LOCAL:
			printf(", local"); break;
		case LLDPMED_POW_SOURCE_BOTH:
			printf(", PSE & local");
			break;
		}
		printf("\n  Power Priority:      ");
		switch (chassis->c_med_pow_priority) {
		case LLDPMED_POW_PRIO_CRITICAL:
			printf("critical"); break;
		case LLDPMED_POW_PRIO_HIGH:
			printf("high"); break;
		case LLDPMED_POW_PRIO_LOW:
			printf("low"); break;
		default:
			printf("unknown");
		}
		printf("\n  Power Value:         ");
		if(chassis->c_med_pow_val < 1024) {
			printf("%u mW", chassis->c_med_pow_val * 100);
		} else {
			printf("reserved");
		}
		printf("\n");
	}
	if (chassis->c_med_hw ||
	    chassis->c_med_sw ||
	    chassis->c_med_fw ||
	    chassis->c_med_sn ||
	    chassis->c_med_manuf ||
	    chassis->c_med_model ||
	    chassis->c_med_asset) {
		printf(" LLDP-MED Inventory:\n");
		if (chassis->c_med_hw)
			printf("   Hardware Revision: %s\n", chassis->c_med_hw);
		if (chassis->c_med_sw)
			printf("   Software Revision: %s\n", chassis->c_med_sw);
		if (chassis->c_med_fw)
			printf("   Firmware Revision: %s\n", chassis->c_med_fw);
		if (chassis->c_med_sn)
			printf("   Serial Number:     %s\n", chassis->c_med_sn);
		if (chassis->c_med_manuf)
			printf("   Manufacturer:      %s\n",
			       chassis->c_med_manuf);
		if (chassis->c_med_model)
			printf("   Model:             %s\n",
			       chassis->c_med_model);
		if (chassis->c_med_asset)
			printf("   Asset ID:          %s\n",
			       chassis->c_med_asset);
	}
}
#endif

void
display_chassis(struct lldpd_chassis *chassis)
{
	char *cid;
	if ((cid = (char *)malloc(chassis->c_id_len + 1)) == NULL)
		fatal(NULL);
	memcpy(cid, chassis->c_id, chassis->c_id_len);
	cid[chassis->c_id_len] = 0;
	switch (chassis->c_id_subtype) {
	case LLDP_CHASSISID_SUBTYPE_IFNAME:
		printf(" ChassisID: %s (ifName)\n", cid);
		break;
	case LLDP_CHASSISID_SUBTYPE_IFALIAS:
		printf(" ChassisID: %s (ifAlias)\n", cid);
		break;
	case LLDP_CHASSISID_SUBTYPE_LOCAL:
		printf(" ChassisID: %s (local)\n", cid);
		break;
	case LLDP_CHASSISID_SUBTYPE_LLADDR:
		printf(" ChassisID: %s (MAC)\n",
		    dump(chassis->c_id, chassis->c_id_len, ETH_ALEN, ':'));
		break;
	case LLDP_CHASSISID_SUBTYPE_ADDR:
		if (*(u_int8_t*)chassis->c_id == 1) {
			printf(" ChassisID: %s (IP)\n",
			    inet_ntoa(*(struct in_addr*)(chassis->c_id +
				    1)));
			break;
		}
	case LLDP_CHASSISID_SUBTYPE_PORT:
	case LLDP_CHASSISID_SUBTYPE_CHASSIS:
	default:
		printf(" ChassisID: %s (unhandled type)\n",
		    dump(chassis->c_id, chassis->c_id_len, 16, ' '));
	}
	printf(" SysName:   %s\n", chassis->c_name);
	printf(" SysDescr:  "); pretty_print(chassis->c_descr);
	printf(" MgmtIP:    %s\n", inet_ntoa(chassis->c_mgmt));
	printf(" Caps:      ");
	display_cap(chassis, LLDP_CAP_OTHER, "Other");
	display_cap(chassis, LLDP_CAP_REPEATER, "Repeater");
	display_cap(chassis, LLDP_CAP_BRIDGE, "Bridge");
	display_cap(chassis, LLDP_CAP_WLAN, "Wlan");
	display_cap(chassis, LLDP_CAP_TELEPHONE, "Tel");
	display_cap(chassis, LLDP_CAP_DOCSIS, "Docsis");
	display_cap(chassis, LLDP_CAP_STATION, "Station");
	printf("\n");
}

#ifdef ENABLE_DOT3
void
display_autoneg(struct lldpd_port *port, int bithd, int bitfd, char *desc)
{
	if (!((port->p_autoneg_advertised & bithd) ||
		(port->p_autoneg_advertised & bitfd)))
		return;
	printf("%s ", desc);
	if (port->p_autoneg_advertised & bithd) {
		printf("(HD");
		if (port->p_autoneg_advertised & bitfd) {
			printf(", FD) ");
			return;
		}
		printf(") ");
		return;
	}
	printf("(FD) ");
}
#endif

void
display_port(struct lldpd_port *port)
{
	char *pid;
#ifdef ENABLE_DOT3
	int i;
#endif

	if ((pid = (char *)malloc(port->p_id_len + 1)) == NULL)
		fatal(NULL);
	memcpy(pid, port->p_id, port->p_id_len);
	pid[port->p_id_len] = 0;
	switch (port->p_id_subtype) {
	case LLDP_PORTID_SUBTYPE_IFNAME:
		printf(" PortID:    %s (ifName)\n", pid);
		break;
	case LLDP_PORTID_SUBTYPE_IFALIAS:
		printf(" PortID:    %s (ifAlias)\n", pid);
		break;
	case LLDP_PORTID_SUBTYPE_LOCAL:
		printf(" PortID:    %s (local)\n", pid);
		break;
	case LLDP_PORTID_SUBTYPE_LLADDR:
		printf(" PortID:    %s (MAC)\n",
		    dump(port->p_id, port->p_id_len, ETH_ALEN, ':'));
		break;
	case LLDP_PORTID_SUBTYPE_ADDR:
		if (*(u_int8_t*)port->p_id == 1) {
			printf(" PortID:    %s (IP)\n",
			    inet_ntoa(*(struct in_addr*)(port->p_id +
				    1)));
			break;
		}
	case LLDP_PORTID_SUBTYPE_PORT:
	case LLDP_PORTID_SUBTYPE_AGENTCID:
	default:
		printf(" ChassisID: %s (unhandled type)\n",
		    dump(port->p_id, port->p_id_len, 16, ' '));
	}
	printf(" PortDescr: "); pretty_print(port->p_descr);
#ifdef ENABLE_DOT3
	if (port->p_aggregid)
		printf("\n   Port is aggregated. PortAggregID:  %d\n",
		    port->p_aggregid);

	if (port->p_autoneg_support || port->p_autoneg_enabled ||
	    port->p_mau_type) {
		printf("\n   Autoneg: %ssupported/%senabled\n",
		    port->p_autoneg_support?"":"not ",
		    port->p_autoneg_enabled?"":"not ");
		if (port->p_autoneg_enabled) {
			printf("   PMD autoneg: ");
			display_autoneg(port, LLDP_DOT3_LINK_AUTONEG_10BASE_T,
			    LLDP_DOT3_LINK_AUTONEG_10BASET_FD,
			    "10Base-T");
			display_autoneg(port, LLDP_DOT3_LINK_AUTONEG_100BASE_TX,
			    LLDP_DOT3_LINK_AUTONEG_100BASE_TXFD,
			    "100Base-T");
			display_autoneg(port, LLDP_DOT3_LINK_AUTONEG_100BASE_T2,
			    LLDP_DOT3_LINK_AUTONEG_100BASE_T2FD,
			    "100Base-T2");
			display_autoneg(port, LLDP_DOT3_LINK_AUTONEG_1000BASE_X,
			    LLDP_DOT3_LINK_AUTONEG_1000BASE_XFD,
			    "100Base-X");
			display_autoneg(port, LLDP_DOT3_LINK_AUTONEG_1000BASE_T,
			    LLDP_DOT3_LINK_AUTONEG_1000BASE_TFD,
			    "1000Base-T");
			printf("\n");
		}
		printf("   MAU oper type: ");
		for (i = 0; operational_mau_type_values[i].value != 0; i++) {
			if (operational_mau_type_values[i].value ==
			    port->p_mau_type) {
				printf("%s\n", operational_mau_type_values[i].string);
				break;
			}
		}
		if (operational_mau_type_values[i].value == 0)
			printf("unknown (%d)\n", port->p_mau_type);
	}
#endif
}

#ifdef ENABLE_DOT1
void
display_vlans(struct lldpd_port *port)
{
	int i = 0;
	struct lldpd_vlan *vlan;
	TAILQ_FOREACH(vlan, &port->p_vlans, v_entries)
		printf("   VLAN %4d: %-20s%c", vlan->v_vid, vlan->v_name,
		    (i++ % 2) ? '\n' : ' ');
	if (i % 2)
		printf("\n");
}
#endif

int
main(int argc, char *argv[])
{
	int s;
	int ch, debug = 1;
	struct interfaces ifs;
#ifdef ENABLE_DOT1
	struct vlans vls;
#endif
	struct lldpd_interface *iff;
	struct lldpd_chassis chassis;
	struct lldpd_port port;
	char sep[80];
	
	/*
	 * Get and parse command line options
	 */
	while ((ch = getopt(argc, argv, "d")) != -1) {
		switch (ch) {
		case 'd':
			debug++;
			break;
		default:
			usage();
		}
	}
	
	log_init(debug);
	memset(sep, '-', 79);
	sep[79] = 0;
	
	if ((s = ctl_connect(LLDPD_CTL_SOCKET)) == -1)
		fatalx("unable to connect to socket " LLDPD_CTL_SOCKET);
	get_interfaces(s, &ifs);

	printf("%s\n", sep);
	printf("    LLDP neighbors\n");
	printf("%s\n", sep);	
	TAILQ_FOREACH(iff, &ifs, next) {
		if ((get_chassis(s, &chassis, iff->name) != -1) &&
		    (get_port(s, &port, iff->name) != -1)) {
			printf("Interface: %s\n", iff->name);
			display_chassis(&chassis);
			printf("\n");
			display_port(&port);
#ifdef ENABLE_DOT1
			if (get_vlans(s, &vls, iff->name) != -1) {
				memcpy(&port.p_vlans, &vls, sizeof(struct vlans));
				if (!TAILQ_EMPTY(&port.p_vlans)) {
					printf("\n");
					display_vlans(&port);
				}
			}
#endif
#ifdef ENABLE_LLDPMED
			if (chassis.c_med_cap_enabled) {
				printf("\n");
				display_med(&chassis);
			}
#endif
			printf("%s\n", sep);
		}
	}
	
	close(s);
	
	return 0;
}
