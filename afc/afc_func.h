#ifndef AFC_FUNC_H_
#define AFC_FUNC_H_

#include <netinet/in.h>

/* ----------------------- FEP includes -------------------------------------*/
#include "common_fep.h"

/* ----------------------- Modbus includes ----------------------------------*/
#include "mb.h"
#include "mbport.h"

/* ----------------------- Type defines -------------------------------------*/
typedef struct afc_equip_info {
	char *equip_id;			// equipment id
	char *mmi_tag;			// mmi tag
	int byte_index;			// byte index of reply message
	int bit_mask;			// bit mask of reply byte
	int status;			// equipment status
} afc_equip_info_t;

typedef struct afc_cfg
{
	char loc[LEN_STATION + 1];	// station location
	char ip_addr[INET_ADDRSTRLEN];	// afc IP address
	int unit_id;			// modbus tcp unit id
	int num_equip;			// array size of equip_info
	afc_equip_info_t *equip_info;	// equipment info array
	int start_reg;			// start of register address
	int num_reg;			// number of registers
	int exit_thread;		// exit thread indicator
} afc_cfg_t;

/* ----------------------- Function defines ---------------------------------*/
eMBErrorCode eMBRegInputCB( UCHAR *, USHORT, USHORT );
eMBErrorCode eMBRegHoldingCB( UCHAR *, USHORT, USHORT, eMBRegisterMode );
eMBErrorCode eMBRegCoilsCB( UCHAR *, USHORT, USHORT, eMBRegisterMode );
eMBErrorCode eMBRegDiscreteCB( UCHAR *, USHORT, USHORT );

#endif // AFC_FUNC_H_
