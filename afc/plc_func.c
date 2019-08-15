/***********************************************************************
Program:
        plc_func.c

Description:
        functional program for plc component

Major functionality:
        - Calculate byte index
        - Read bytes from GE PLC via Service Request Transport Protocol
	  (SRTP)

Revision history:
	Date:		Summary:				Author:
	TBD		Initial release				Simon Lau

************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if defined LINUX || defined CYGWIN
#include <unistd.h>
#include <sys/time.h>
#include <fcntl.h>
#define UNIX_STYLE
#endif // LINUX || CYGWIN

#include "libOpenSRTP/openSRTP.h"
#include "libOpenSRTP/openSocket.h"
#include "plc_func.h"

//#define PLC_PORT		18245
#define PLC_GE_MIN_READ_BYTE	6

// calculate the corresponding byte index from bit index
//
// input : bit - bit index
// output: N/A
//
// return: byte index
int srtp_cal_byte(int bit)
{
	int byte;

	// byte = (bit + 1) >> 3; // equivalent but may overflow
	byte = bit >> 3;
	if (bit & 0x7)
		byte++;
	return byte;
}

// read I% memory bytes via srtp
//
// input : fds - srtp file descriptor set
//	 : start_byte - start reading byte (inclusive)
//	 : stop_byte - stop reading byte (inclusive)
// output: buffer - pointer to pointer of the received message
//
// return: 0 - fail, 1 success
int srtp_Iread_bytes(srtpOS_IOhandle *fds, int start_byte, int stop_byte, uc **buffer)
{
	int read_len;
	int bytes_read = 0;
	srtpConnection *dc = NULL;

	if (!fds)
		return 0;

	dc = srtpNewConnection(*fds);
	if (!dc)
		return 0;

	read_len = stop_byte - start_byte + 1;
	// minimum read 6 bytes
	if (read_len < PLC_GE_MIN_READ_BYTE)
		read_len = PLC_GE_MIN_READ_BYTE;

	//printf("ConnectPLC\n");
	if (srtpConnectPLC(dc) == 0)
	{
		int r;
		int area = srtpAreaIByte;
		*buffer = malloc(read_len * sizeof(uc));

 		//printf("ConnectPLC success\n");

		if (!(*buffer))
		{
			printf("Allocate buffer failed\n");
			goto err;
		}

		//r = srtpReadBytes(dc, srtpAreaIByte, 209, r_len, buffer);
		//I%1665 => 1665/8 = 208.125 => 209
		r = srtpReadBytes(dc, area, start_byte, read_len, *buffer);
		//printf("Finish reading, ret=%d\n", r);
		if (r == 0)
			bytes_read = read_len;
	} // srtpConnectPLC

err:
	free(dc);
	return bytes_read;
}

// close socket
// as a function pair to openSocket in openSocket.c
//
// input : fd - srtp file descriptor
// output: buffer - N/A
//
// return: N/A
void closeSocket(int fd)
{
	close(fd);
}
