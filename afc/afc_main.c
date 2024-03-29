/***********************************************************************
Program:
	afc_main.c

Description:
	Component program for handling AFC interface

Major functionality:
	- Keep check with AFC interface connection
	- Write heartbeat to AFC equipment
	- Input and event log
        - Inform FEP when there is change

Revision history:
	Date:		Summary:				Author:
	TBD		Initial release				Oscar Yeung

************************************************************************/

/* ----------------------- Standard C Libs includes --------------------------*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>

/* ----------------------- Modbus includes ----------------------------------*/
#include "afc_main.h"
#include "afc_func.h"
#include "mb.h"
#include "mbport.h"

/* ----------------------- Defines ------------------------------------------*/
#define PROG				"afc"

#define DIR_LOG_AFC			DIR_LOG"/afc"			// log directory
#define DIR_ARCHIVE_AFC			DIR_ARCHIVE"/afc"		// archive directory
#define DIR_CFG_AFC			DIR_CFG"/afc"			// config directory
#define DIR_TMP_AFC			DIR_TMP"/afc"			// directory for temporary files

#define PATH_CONFIG			DIR_CFG_AFC"/afc_config.txt"	// configure file path
#define PATH_TMP_AFC_STATUS		DIR_TMP_AFC"/afc_status_tmp.txt"// temporary afc status file path

/* ----------------------- Static variables ---------------------------------*/
static pthread_mutex_t xLock = PTHREAD_MUTEX_INITIALIZER;
static enum ThreadState
{
	STOPPED,
	RUNNING,
	SHUTDOWN
} ePollThreadState;

/* ----------------------- Static functions ---------------------------------*/
static BOOL bCreatePollingThread( void );
static enum ThreadState eGetPollingThreadState( void );
static void eSetPollingThreadState( enum ThreadState eNewState );
static void* pvPollingThread( void *pvParameter );
static int afc_create_config(afc_cfg_t *cfg);
static void afc_sig_handler(int signo);

/* ----------------------- Start implementation -----------------------------*/
int main( int argc, char *argv[] )
{
	int		iExitCode;
	afc_cfg_t	ctCfgFile;

	/*if (access(PATH_AFC_STATUS, F_OK) == 0)
	{
		remove(PATH_AFC_STATUS);
	}

	if (afc_create_config(&ctCfgFile) == 0)
	{
		printf("Init afc config failed\n");
		iExitCode = EXIT_FAILURE;
		goto Err;
	}*/

	// signal handler
	if (signal(SIGINT, afc_sig_handler) == SIG_ERR)
	{
		printf("Can't catch SIGINT\n");
		//log_print(LOG_LVL_CRITICAL, DIR_LOG_PLC, cfg.loc, LOG_TYPE_EVT, "Can't catch SIGINT\n");
		iExitCode = EXIT_FAILURE;
		goto Err;
	}

	xMBTCPClientInit( "192.168.121.1" );
	
	if ( eMBTCPInit( MB_TCP_PORT_USE_DEFAULT ) != MB_ENOERR )
	{
		fprintf( stderr, "%s: can't initialize modbus stack!\r\n", PROG );
		iExitCode = EXIT_FAILURE;
	}
	else
	{
		eSetPollingThreadState( STOPPED );

		if( bCreatePollingThread(  ) != TRUE )
		{
			printf(  "Can't start protocol stack! Already running?\r\n"  );
		}

		eSetPollingThreadState( SHUTDOWN );
	
		/* Release hardware resources. */
		( void )eMBClose(  );
		iExitCode = EXIT_SUCCESS;
	}

Err:	

	return iExitCode;
}

//int
//main( int argc, char *argv[] )
//{
//    int             iExitCode;
//    CHAR           cCh;
//    BOOL            bDoExit;
//
//    if( eMBTCPInit( MB_TCP_PORT_USE_DEFAULT ) != MB_ENOERR )
//    {
//        fprintf( stderr, "%s: can't initialize modbus stack!\r\n", PROG );
//        iExitCode = EXIT_FAILURE;
//    }
//    else
//    {
//        eSetPollingThreadState( STOPPED );
//        /* CLI interface. */
//        printf(  "Type 'q' for quit or 'h' for help!\r\n"  );
//        bDoExit = FALSE;
//        do
//        {
//            printf(  "> "  );
//            cCh = getchar(  );
//            switch ( cCh )
//            {
//            case  'q' :
//                bDoExit = TRUE;
//                break;
//            case  'd' :
//                eSetPollingThreadState( SHUTDOWN );
//                break;
//            case  'e' :
//                if( bCreatePollingThread(  ) != TRUE )
//                {
//                    printf(  "Can't start protocol stack! Already running?\r\n"  );
//                }
//                break;
//            case  's' :
//                switch ( eGetPollingThreadState(  ) )
//                {
//                case RUNNING:
//                    printf(  "Protocol stack is running.\r\n"  );
//                    break;
//                case STOPPED:
//                    printf(  "Protocol stack is stopped.\r\n"  );
//                    break;
//                case SHUTDOWN:
//                    printf(  "Protocol stack is shuting down.\r\n"  );
//                    break;
//                }
//                break;
//            case  'h':
//                printf(  "FreeModbus demo application help:\r\n" );
//                printf(  "  'd' ... disable protocol stack.\r\n"  );
//                printf(  "  'e' ... enabled the protocol stack\r\n"  );
//                printf(  "  's' ... show current status\r\n"  );
//                printf(  "  'q' ... quit applicationr\r\n"  );
//                printf(  "  'h' ... this information\r\n"  );
//                printf(  "\r\n"  );
//                break;
//            default:
//                if( cCh != '\n' )
//                {
//                    printf(  "illegal command '%c'!\r\n" , cCh );
//                }
//                break;
//            }
//
//            /* eat up everything untill return character. */
//            while( cCh != '\n' )
//            {
//                cCh = getchar(  );
//            }
//        }
//        while( !bDoExit );
//
//        /* Release hardware resources. */
//        ( void )eMBClose(  );
//        iExitCode = EXIT_SUCCESS;
//    }
//    return iExitCode;
//}

BOOL bCreatePollingThread( void )
{
	BOOL		bResult;
	pthread_t	xThread;

	if( eGetPollingThreadState(  ) == STOPPED )
	{
		if( pthread_create( &xThread, NULL, pvPollingThread, NULL ) != 0 )
		{
			/* Can't create the polling thread. */
			bResult = FALSE;
		}
		else
		{
			bResult = TRUE;
		}
	}
	else
	{
		bResult = FALSE;
	}

	return bResult;
}

void* pvPollingThread( void *pvParameter )
{
	eSetPollingThreadState( RUNNING );

	if( eMBEnable(  ) == MB_ENOERR )
	{
		do
		{
			if( eMBPoll(  ) != MB_ENOERR )
			{
				break;
			}
		}
		while( eGetPollingThreadState(  ) != SHUTDOWN );
	}

	( void )eMBDisable(  );

	eSetPollingThreadState( STOPPED );

	return 0;
}

enum ThreadState eGetPollingThreadState(  )
{
	enum ThreadState eCurState;

	( void )pthread_mutex_lock( &xLock );
	eCurState = ePollThreadState;
	( void )pthread_mutex_unlock( &xLock );

	return eCurState;
}

void eSetPollingThreadState( enum ThreadState eNewState )
{
	( void )pthread_mutex_lock( &xLock );
	ePollThreadState = eNewState;
	( void )pthread_mutex_unlock( &xLock );
}

// create the afc configuration
//
// input : N/A
// output: cfg - afc configuration pointer
//
// return: 0 - fail, 1 - success
static int afc_create_config(afc_cfg_t *cfg)
{
	#define MB_CFG_IP   		"DEV_IP"
	#define MB_CFG_NUM_EQUIP	"NUM_EQUIP"
	#define MB_CFG_EQUIP		"EQUIP"
	#define MB_CFG_TYPE_AFC		0

	FILE *fp;
	char buf[256];
	int found = 0;
	char *common_cfg_elements[] = { "location" };
	int num_common_cfg_element = sizeof(common_cfg_elements) / sizeof(char *);
	char *afc_cfg_elements[] = { MB_CFG_IP, MB_CFG_NUM_EQUIP, MB_CFG_EQUIP };
	int num_afc_cfg_element = sizeof(afc_cfg_elements) / sizeof(char *);
	int array_used = 0;
	int min_bit = 99999; // arbitary large number
	int max_bit = 0;

	if (!cfg)
	{
		return 0;
	}

	cfg->loc[0] = '\0';
	cfg->ip_addr[0] = '\0';
	cfg->unit_id = 0;
	cfg->num_equip = -1;
	cfg->equip_info = NULL;
	cfg->start_reg = 0;
	cfg->num_reg = 0;
	
	cfg->exit_thread = 0;

	fp = fopen(PATH_LOCATION, "r");
	if (!fp)
	{
		return 0;
	}

	if (fscanf(fp, "%255s", buf) == 1 && strlen(buf) == LEN_STATION)
	{
		if (cfg->loc[0] == '\0')
		{
			found++;
		}
		snprintf(cfg->loc, LEN_STATION + 1, "%s", buf);
//		log_print(LOG_LVL_CRITICAL, DIR_LOG_AFC, cfg->loc, LOG_TYPE_EVT,
//			"afc started at location = %s\n", cfg->loc);
	}
	fclose(fp);

	fp = fopen(PATH_CONFIG, "r");
	if (!fp)
	{
		return 0;
	}

	//while (fscanf(fp, "%255s", buf) != EOF)
	while (fgets(buf, sizeof(buf), fp))
	{
		char *p = strchr(buf, '=');
		char *tmp;

		// invalid format or it is a comment
		if (!p || buf[0] == '#')
			continue;

		// remove the newline characters
		tmp = strchr(buf, '\n');
		if (tmp)
			*tmp = '\0';

		tmp = strchr(buf, '\r');
		if (tmp)
			*tmp = '\0';

		*p = '\0';
		if (strcmp(buf, MB_CFG_IP) == 0)
		{
			if (cfg->ip_addr[0] == '\0')
				found++;
			
			snprintf(cfg->ip_addr, INET_ADDRSTRLEN, "%s", p + 1);
		}
		else if (strcmp(buf, MB_CFG_EQUIP) == 0)
		{
			int num_equip = (int) strtol(p + 1, NULL, 10);

			if (num_equip > 0)
			{
				if (cfg->num_equip == -1)
					found++;

				cfg->num_equip = num_equip;
			}
		}
		else if (strcmp(buf, MB_CFG_EQUIP) == 0)
		{
			if (cfg->equip_info == NULL && cfg->num_equip > 0)
			{
				cfg->equip_info = calloc(cfg->num_equip, sizeof(afc_equip_info_t));
				found++;
			}

			if (array_used < cfg->num_equip)
			{
				afc_equip_info_t *info = &(cfg->equip_info[array_used]);
				char *token;
				char *ptr = p + 1;
				int i = 0;
				int num_field = 4; // type, id, index in bit, mmi_tag
				int found = 0;

				for (i = 0; i < num_field; i++)
				{
					token = strtok(ptr, ",");
					if (!token)
						break;
					//test
					//printf("%d: token>%s\n", i, token);

					// MODBUS type
					if (i == 0)
					{
						int type = strtol(token, NULL, 10);

						if (type != MB_CFG_TYPE_AFC)
							break;
						found++;
					}
					// equipment id
					if (i == 1)
					{
						free(info->equip_id); // for safety
						info->equip_id = strdup(token);
						found++;
					}
					// PLC bit index
					else if (i == 2)
					{
						/*int bit_idx = strtol(token, NULL, 10);
						int mask_idx = bit_idx & 0x7;

						info->byte_index = srtp_cal_byte(bit_idx);
						if (mask_idx == 0)
							info->bit_mask = 0x80;
						else
							info->bit_mask = 1 << (mask_idx - 1);

						if (bit_idx < min_bit)
							min_bit = bit_idx;

						if (bit_idx > max_bit)
							max_bit = bit_idx;
						found++;*/
					}
					// MMI tag
					else if (i == 3)
					{
						free(info->mmi_tag); // for safety
						info->mmi_tag = strdup(token);
						found++;
					}
					ptr = NULL;
				}
				if (found == num_field)
				{
					info->status = STATUS_INIT;
					array_used++;
				}
			}
		}
	}
	fclose(fp);

	if (found < num_common_cfg_element + num_afc_cfg_element)
	{
		printf("Incomplete configure file\n");
//		log_print(LOG_LVL_CRITICAL, DIR_LOG_AFC, cfg->loc, LOG_TYPE_EVT,
//			"%s: Incomplete configure file\n", __func__);
		return 0;
	}

//	cfg->base_byte_idx = srtp_cal_byte(min_bit);
//	cfg->final_byte_idx = srtp_cal_byte(max_bit);
	return 1;	
}

// signal handler
// stop the application by setting g_stop = 1 when received SIGINT
//
// input : signo - signal number 
// output: N/A
//
// return: N/A
static void afc_sig_handler(int signo)
{
	if (signo == SIGINT && g_stop == 0)
	{
		printf("%s received SIGINT\n", __FILE__);
		g_stop = 1;
	}
}


