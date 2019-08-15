#ifndef PLC_FUNC_H_
#define PLC_FUNC_H_

#include <netinet/in.h>

#include "libOpenSRTP/openSRTP.h"
#include "libOpenSRTP/openSocket.h"
#include "common_fep.h"

typedef struct plc_equip_info {
	char *equip_id;			// equipment id
	char *mmi_tag;			// mmi tag
	int byte_index;			// byte index of reply message
	int bit_mask;			// bit mask of reply byte
	int status;			// equipment status
} plc_equip_info_t;

typedef struct plc_cfg
{
	char loc[LEN_STATION + 1];	// station location
	char ip_addr[INET_ADDRSTRLEN];	// plc IP address
	int num_equip;			// array size of equip_info
	plc_equip_info_t *equip_info;	// equipment info array
	int base_byte_idx;		// base byte index of equipment
	int final_byte_idx;		// final byte index of equipment
	int exit_thread;		// exit thread indicator
} plc_cfg_t;

int srtp_cal_byte(int bit);
int srtp_Iread_bytes(srtpOS_IOhandle *fds, int start_byte, int stop_byte, uc **buffer);
void closeSocket(int fd);
#endif // PLC_FUNC_H_
