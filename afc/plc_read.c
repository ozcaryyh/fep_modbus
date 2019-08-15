/***********************************************************************
Program:
	plc_read.c

Description:
	Component program for handling Programmable Logic controller (PLC)

Major functionality:
	- Keep check with PLC connection
	- Read equipment status via digital input (DI) of a particular
	  PLC address
	- Input and event log
        - Inform FEP when there is change

Revision history:
	Date:		Summary:				Author:
	TBD		Initial release				Simon Lau

************************************************************************/

#include <stdio.h>
#include <errno.h>
#include <fcntl.h> 
#include <string.h>
#include <termios.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <dirent.h>
#include <pthread.h>
#include <unistd.h>
#include <netinet/in.h>

#include "plc_read.h"
#include "plc_func.h"

#define DIR_LOG_PLC			DIR_LOG"/plc"			// log directory
#define DIR_ARCHIVE_PLC			DIR_ARCHIVE"/plc"		// archive directory
#define DIR_CFG_PLC			DIR_CFG"/plc"			// config directory
#define DIR_TMP_PLC			DIR_TMP"/plc"			// directory for temporary files

#define PATH_CONFIG			DIR_CFG_PLC"/plc_config.txt"	// configure file path
#define PATH_TMP_PLC_STATUS		DIR_TMP_PLC"/plc_status_tmp.txt"// temporary plc status file path

#define PLC_PORT             		18245

// global variable as indicator of exit
int g_stop = 0;

// inform fep as there is status change or on demand request
//
// input : cfg - configuration pointer
// output: N/A
//
// return: N/A
static void plc_inform_fep(plc_cfg_t *cfg)
{
	FILE *fp;
	char cmd[256];
	int ret;
	int log_lvl;

	if (!cfg)
		return;

	fp = fopen(PATH_TMP_PLC_STATUS, "w");
	if (fp)
	{
		int i;
		plc_equip_info_t *info = cfg->equip_info;

		for (i = 0; i < cfg->num_equip; i++)
			fprintf(fp, "%s:%d,", info[i].equip_id, info[i].status);
		fclose(fp);
	}
	else
	{
		printf("Fail to write %s\n", PATH_TMP_PLC_STATUS);
		log_print(LOG_LVL_CRITICAL, DIR_LOG_PLC, cfg->loc, LOG_TYPE_EVT, "%s: Fail to write %s\n",
			__func__, PATH_TMP_PLC_STATUS);
		return;
	}

	// move
	file_move(PATH_TMP_PLC_STATUS, DIR_BASE_PLC, FILE_PLC_STATUS);

	// inform FEP there is change in status
	snprintf(cmd, sizeof(cmd), "killall -SIGUSR1 fep\n");
	ret = system(cmd);

	#if 1
	log_lvl = LOG_LVL_CRITICAL;
	#else
	if (ret == 0)
		log_lvl = LOG_LVL_DETAIL;
	else
		log_lvl = LOG_LVL_CRITICAL;
	#endif //if 1

	log_print(log_lvl, DIR_LOG_PLC, cfg->loc, LOG_TYPE_EVT,
		"Inform FEP return %d\n", ret);
}

// update plc status to STATUS_FAULT
//
// input : cfg - configuration pointer
// output: N/A
//
// return: 0 - no change, 1 - there is change
static int plc_update_fault_status(plc_cfg_t *cfg)
{
	int i;
	plc_equip_info_t *info;
	int chg = 0;

	if (!cfg)
		return 0;

	info = cfg->equip_info;

	for (i = 0; i < cfg->num_equip; i++)
	{
		if (info[i].status != STATUS_FAULT)
		{
			if (chg == 0)
			{
				chg = 1;
				log_print(LOG_LVL_CRITICAL, DIR_LOG_PLC, cfg->loc, LOG_TYPE_EVT,
					"plc update fault status, status from %d to %d\n",
					info[i].status, STATUS_FAULT);
			}
			info[i].status = STATUS_FAULT;
		}
	}
	return chg;
}

// worker for housekeep plc log
// compress old plc log into archive folder
//
// input : arg - configuration pointer
// output: N/A
//
// return: NULL
static void *plc_log_housekeep_worker(void *arg)
{
	plc_cfg_t *cfg = arg;
	struct timespec ts;
	struct tm *timeinfo;
	char in_file[256];
	int checked = 0;

	if (!cfg)
	{
		printf("%s: invalid argument\n", __func__);
		return NULL;
	}

	while (!g_stop)
	{
		clock_gettime(CLOCK_REALTIME, &ts);	
		timeinfo = localtime(&(ts.tv_sec));
		// once per hour
		if (timeinfo->tm_min == 37)
		{
			//printf("tm_min: %d, checked=%d\n", timeinfo->tm_min, checked);
			if (checked == 0)
			{
				DIR *d;
				struct dirent *dir;
				char *dir_file, *ptr, *ptr2;

				log_gen_filename(timeinfo, cfg->loc, LOG_TYPE_IN, DIR_LOG_PLC,
					in_file, sizeof(in_file));
				ptr = strrchr(in_file, '/');
				ptr2 = strrchr(in_file, '_');
				if (ptr2)
					*ptr2 = '\0';
\
				d = opendir(DIR_LOG_PLC);
				if (d)
				{
					while ((dir = readdir(d)) != NULL)
					{
						// skip directory or swp file
						if (dir->d_type != DT_REG || dir->d_name[0] == '.')
							continue;

						dir_file = dir->d_name;
						// expect in_file format: .../xxx_yyyy-mm-dd_hh_in.txt, compare len = (ptr2 - 1) - (ptr + 1) + 1
						printf("dir_file: %s, ptr: %s, len: %ld\n",
							dir_file, ptr + 1, ptr2 - ptr - 1);

						log_print(LOG_LVL_DETAIL, DIR_LOG_PLC, cfg->loc, LOG_TYPE_EVT,
							"dir_file: %s, ptr: %s, len: %ld\n",
							dir_file, ptr + 1, ptr2 - ptr - 1);

						if (strncmp(dir_file, ptr + 1, ptr2 - ptr - 1))
							log_compress_file(DIR_LOG_PLC, dir_file, DIR_ARCHIVE_PLC);
					}
					closedir(d);
				}
				checked = 1;
			}
		}
		else
		{
			checked = 0;
		}
		sleep(1);
	}
	printf("%s: prepare for exit\n", __func__);
	log_print(LOG_LVL_CRITICAL, DIR_LOG_PLC, cfg->loc, LOG_TYPE_EVT, "%s: prepare for exit\n", __func__);

	return NULL;
}

// signal handler
// stop the application by setting g_stop = 1 when received SIGINT
//
// input : signo - signal number 
// output: N/A
//
// return: N/A
static void plc_sig_handler(int signo)
{
	if (signo == SIGINT && g_stop == 0)
	{
		printf("%s received SIGINT\n", __FILE__);
		g_stop = 1;
	}
}

// destroy the plc configuration
//
// input : cfg - plc configuration pointer
// output: N/A
//
// return: 0 - fail, 1 - success
static int plc_destroy_config(plc_cfg_t *cfg)
{
	int i;

	if (!cfg)
		return 0;

	if (cfg->equip_info)
	{
		for (i = 0; i < cfg->num_equip; i++)
		{
			plc_equip_info_t *info = &(cfg->equip_info[i]);

			if (info)
			{
				free(info->equip_id);
				free(info->mmi_tag);
			}
		}
		free(cfg->equip_info);
	}	
	return 1;
}

// create the plc configuration
//
// input : N/A
// output: cfg - plc configuration pointer
//
// return: 0 - fail, 1 - success
static int plc_create_config(plc_cfg_t *cfg)
{
	#define PLC_CFG_IP   		"PLC_IP"
	#define PLC_CFG_NUM_EQUIP	"NUM_GE_EQUIP"
	#define PLC_CFG_EQUIP		"EQUIP"
	#define PLC_CFG_TYPE_GE		0

	FILE *fp;
	char buf[256];
	int found = 0;
	char *common_cfg_elements[] = { "location" };
	int num_common_cfg_element = sizeof(common_cfg_elements) / sizeof(char *);
	char *plc_cfg_elements[] = { PLC_CFG_IP, PLC_CFG_NUM_EQUIP, PLC_CFG_EQUIP };
	int num_plc_cfg_element = sizeof(plc_cfg_elements) / sizeof(char *);
	int array_used = 0;
	int min_bit = 99999; // arbitary large number
	int max_bit = 0;

	if (!cfg)
		return 0;

	cfg->loc[0] = '\0';
	cfg->ip_addr[0] = '\0';
	cfg->num_equip = -1;
	cfg->equip_info = NULL;
	cfg->base_byte_idx = 0;
	cfg->final_byte_idx = 0;
	
	cfg->exit_thread = 0;

	fp = fopen(PATH_LOCATION, "r");
	if (!fp)
		return 0;

	if (fscanf(fp, "%255s", buf) == 1 && strlen(buf) == LEN_STATION)
	{
		if (cfg->loc[0] == '\0')
			found++;
		snprintf(cfg->loc, LEN_STATION + 1, "%s", buf);
		log_print(LOG_LVL_CRITICAL, DIR_LOG_PLC, cfg->loc, LOG_TYPE_EVT,
			"plc started at location = %s\n", cfg->loc);
	}
	fclose(fp);

	fp = fopen(PATH_CONFIG, "r");
	if (!fp)
		return 0;

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
		if (strcmp(buf, PLC_CFG_IP) == 0)
		{
			if (cfg->ip_addr[0] == '\0')
				found++;
			
			snprintf(cfg->ip_addr, INET_ADDRSTRLEN, "%s", p + 1);
		}
		else if (strcmp(buf, PLC_CFG_NUM_EQUIP) == 0)
		{
			int num_equip = (int) strtol(p + 1, NULL, 10);

			if (num_equip > 0)
			{
				if (cfg->num_equip == -1)
					found++;

				cfg->num_equip = num_equip;
			}
		}
		else if (strcmp(buf, PLC_CFG_EQUIP) == 0)
		{
			if (cfg->equip_info == NULL && cfg->num_equip > 0)
			{
				cfg->equip_info = calloc(cfg->num_equip, sizeof(plc_equip_info_t));
				found++;
			}

			if (array_used < cfg->num_equip)
			{
				plc_equip_info_t *info = &(cfg->equip_info[array_used]);
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

					// PLC type
					if (i == 0)
					{
						int type = strtol(token, NULL, 10);

						if (type != PLC_CFG_TYPE_GE)
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
						int bit_idx = strtol(token, NULL, 10);
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
						found++;
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

	if (found < num_common_cfg_element + num_plc_cfg_element)
	{
		printf("Incomplete configure file\n");
		log_print(LOG_LVL_CRITICAL, DIR_LOG_PLC, cfg->loc, LOG_TYPE_EVT,
			"%s: Incomplete configure file\n", __func__);
		return 0;
	}

	cfg->base_byte_idx = srtp_cal_byte(min_bit);
	cfg->final_byte_idx = srtp_cal_byte(max_bit);
	return 1;	
}

// read the General Electric plc status
//
// input : fds - file descriptor pointer set
//	 : cfg - plc configuration pointer
// output: chg - indicator for status change
//
// return: 0 - fail, 1 - success
static int plc_read_GE_status(srtpOS_IOhandle *fds, plc_cfg_t *cfg, int *chg)
{
	if (!fds || !cfg)
		return 0;

	fds->rfd = openSocket(PLC_PORT, cfg->ip_addr);
	fds->wfd = fds->rfd;
	if (fds->rfd > 0)
	{
		uc *dup_buf = NULL;
		int bytes_read = srtp_Iread_bytes(fds, cfg->base_byte_idx, cfg->final_byte_idx, &dup_buf);

		if (bytes_read > 0)
		{
			uc *tmp = dup_buf;
			char log_buf[2048];
			int i;

			tmp = dup_buf;
			log_buf[0] = '\0';
			for (i = 0; i < cfg->num_equip; i++)
			{
				plc_equip_info_t *info = &(cfg->equip_info[i]);
				int len = strlen(log_buf);

				if (info)
				{
					int idx = info->byte_index - cfg->base_byte_idx;
					int status;

					if (idx >= bytes_read)
					{
						printf("Fail to read %s's status, %d>=%d\n",
							info->equip_id, idx, bytes_read);
						log_print(LOG_LVL_CRITICAL, DIR_LOG_PLC, cfg->loc, LOG_TYPE_EVT,
							"Fail to read %s's status, %d>=%d\n",
							info->equip_id, idx, bytes_read); 
						continue;
					}

					status = tmp[idx] & info->bit_mask ? 1 : 0;

					snprintf(log_buf + len, sizeof(log_buf) - len, "%s%d",
						len > 0 ? " " : "", status); 
					//printf("id=%s, idx=%d, tmp[idx]=0x%02x, %s\n",
					//	info->equip_id, idx, tmp[idx], log_buf);
					
					// check for status change
					if (info->status != status)
					{
						printf("%s status change from %d to %d\n",
							info->equip_id, info->status, status);
						log_print(LOG_LVL_CRITICAL, DIR_LOG_PLC, cfg->loc, LOG_TYPE_EVT,
							"%s status change from %d to %d\n",
							info->equip_id, info->status, status); 
						if (*chg == 0)
							*chg = 1;
					}

					info->status = status;
					//printf("id=%s, status=%d\n", info->equip_id, info->status);
				}
			}
			log_print(LOG_LVL_CRITICAL, DIR_LOG_PLC, cfg->loc, LOG_TYPE_IN, "%s\n", log_buf); 
		} // bytes_read
		else
		{
			log_print(LOG_LVL_DETAIL, DIR_LOG_PLC, cfg->loc, LOG_TYPE_IN, "fail to read srtp\n"); 
		}
		//printf("Now disconnecting\n");
		free(dup_buf);
		closeSocket(fds->rfd);
	} // fds.rfd
	else
	{
		printf("Couldn't open port of %s\n", cfg->ip_addr);	
		log_print(LOG_LVL_CRITICAL, DIR_LOG_PLC, cfg->loc, LOG_TYPE_IN,
			"Couldn't open port of %s\n", cfg->ip_addr); 
		return 0;
	}
	return 1;
}

int main(int argc, char *argv[])
{
	srtpOS_IOhandle fds;
	int exit_code = 0;
	plc_cfg_t cfg;
	//int i;
	pthread_t housekeep_thread;
	void *ret;

	if (access(PATH_PLC_STATUS, F_OK) == 0)
		remove(PATH_PLC_STATUS);

	if (plc_create_config(&cfg) == 0) {
		printf("Init plc config failed\n");
		exit_code = -1;
		goto Err;
	}

	// signal handler
	if (signal(SIGINT, plc_sig_handler) == SIG_ERR)
	{
		printf("Can't catch SIGINT\n");
		log_print(LOG_LVL_CRITICAL, DIR_LOG_PLC, cfg.loc, LOG_TYPE_EVT, "Can't catch SIGINT\n");
		exit_code = -1;
		goto Err;
	}

	pthread_create(&housekeep_thread, NULL, plc_log_housekeep_worker, &cfg);

	printf("PLC finish initialization\n");
	log_print(LOG_LVL_CRITICAL, DIR_LOG_PLC, cfg.loc, LOG_TYPE_EVT, "Finish initialization\n");

// test
/*
	printf("ip=%s, num_eqp=%d\n", cfg.ip_addr, cfg.num_equip);
	for (i = 0; i < cfg.num_equip; i++)
	{
		plc_equip_info_t *info = &(cfg.equip_info[i]);		
	
		if (info)
		{
			printf("id=%s, mmi=%s, idx=%d, mask=0x%08X\n",
				info->equip_id, info->mmi_tag, info->byte_index, info->bit_mask);
		}
	}
	printf("base=%d, final=%d\n", cfg.base_byte_idx, cfg.final_byte_idx);
*/	
	// read loop
	while (!g_stop)
	{
		int chg = 0;

		// read
		if (plc_read_GE_status(&fds, &cfg, &chg))
		{
			if (chg)
				plc_inform_fep(&cfg);
		}
		else
		{
			if (plc_update_fault_status(&cfg))
				plc_inform_fep(&cfg);
		}
		sleep(1);
	}
	log_print(LOG_LVL_CRITICAL, DIR_LOG_PLC, cfg.loc, LOG_TYPE_EVT, "Main: received SIGINT\n");

	pthread_join(housekeep_thread, &ret);
Err:
	// destroy_config
	plc_destroy_config(&cfg);

	log_print(LOG_LVL_CRITICAL, DIR_LOG_PLC, cfg.loc, LOG_TYPE_EVT, "Exit\n");
	return exit_code;
}
