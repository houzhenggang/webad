#ifndef __TCP_STREAM_H__
#define __TCP_STREAM_H__

#include "list.h"

/*typedef enum
{
  TCP_ESTABLISHED = 1,
  TCP_SYN_SENT,
  TCP_SYN_RECV,
  TCP_FIN_WAIT1,
  TCP_FIN_WAIT2,
  TCP_TIME_WAIT,
  TCP_CLOSE,
  TCP_CLOSE_WAIT,
  TCP_LAST_ACK,
  TCP_LISTEN,
  TCP_CLOSING  
}TCP_STATE;
*/
typedef enum
{
  TCP_STATE_OTHER,
  TCP_STATE_JUST_EST,
  TCP_STATE_CLOSE,
  TCP_STATE_RESET,
  TCP_STATE_DATA,
  TCP_STATE_MAX  
}TCP_STATE;

enum
{
	RESULT_IGNORE,
	RESULT_CACHE,
	RESULT_FROM_CLIENT,
	RESULT_FROM_SERVER,
	RESULT_OTHER
};
struct tuple4
{
	unsigned short sp,dp;
	unsigned long sip,dip;
};

struct skb_buf
{
	struct list_head list; 
	int packet_id;
	int pload_len;
	unsigned char* pload;
	unsigned long seq,ack_seq;	
	unsigned int data_len;
	char result;
};

struct tcp_stream
{
	struct list_head list; 
	long last_time;
	struct tuple4 addr;
	TCP_STATE state;
	char from_client;
	struct list_head ofo_from_server_head;//only need recompose data from server 
	unsigned long curr_seq;
	unsigned int curr_data_len;
	void (*callback)(void*);
};

void process_tcp(struct skb_buf *skb ,void (*callback)(void*));
void init_tcp_stream();

#endif 
