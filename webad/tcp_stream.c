#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <netinet/in.h>

#include "util.h"
#include "tcp_stream.h"

#define MAX_TCP_STREAM_NUN 256
#define MAX_TIMEOUT_SEC 20
PRIVATE struct list_head tcp_stream_list_head; 
PRIVATE int tcp_stream_num=0;

int add2ofo_list(struct tcp_stream* tcps, struct skb_buf* skb)
{
	unsigned char* pload;
	struct skb_buf *new_skb = test_malloc(sizeof(struct skb_buf));
	memcpy(new_skb , skb , sizeof(struct skb_buf));
	pload = (unsigned char*)test_malloc(skb->pload_len);
	memcpy(pload , skb->pload , skb->pload_len);
	new_skb->pload = pload;
	la_list_add_tail(&new_skb->list , &tcps->list);
	
	return 0;
}

void free_ofo_list(struct tcp_stream* tcps)
{
	struct skb_buf *cursor , *tmp;
    list_for_each_entry_safe(cursor, tmp, &tcps->ofo_from_server, list)
    {
    	cursor->result=RESULT_IGNORE;
		tcps->callback(cursor);
		la_list_del(&cursor->list);
    	test_free(cursor->pload);
		test_free(cursor);
	}
}

struct tcp_stream* find_by_tuple4(struct tuple4* addr)
{
	struct tcp_stream *cursor , *tmp;
    list_for_each_entry_safe(cursor, tmp, &tcp_stream_list_head, list)
    {
            if(addr->sip == cursor->addr.sip &&
                    addr->dip == cursor->addr.dip &&
                    addr->sp == cursor->addr.sp &&
                    addr->dp == cursor->addr.dp)
            {
            	cursor->from_client=1;
                return cursor;
            }
			else if(addr->sip == cursor->addr.dip &&
                    addr->dip == cursor->addr.sip &&
                    addr->sp == cursor->addr.dp &&
                    addr->dp == cursor->addr.sp)
			{
				cursor->from_client=0;
                return cursor;
			}
			else
				continue;
    }
    return NULL;
}

struct tcp_stream* new_tcp_stream(struct tcp_stream* tcps,struct skb_buf* skb)
{
	struct tcp_stream* new_tcps;
	
	new_tcps=(struct tcp_stream*)test_malloc(sizeof(struct tcp_stream));
	memcpy(new_tcps , tcps , sizeof(struct tcp_stream));
	INIT_LIST_HEAD(&new_tcps->ofo_from_server);
	new_tcps->last_time = get_current_sec();
	memcpy(&new_tcps->skb , skb , sizeof(struct skb_buf));
	la_list_add_tail(&(new_tcps->list), &tcp_stream_list_head);
	tcp_stream_num++;
	return new_tcps;
}

void free_tcp_stream(struct tcp_stream* tcps)
{
	free_ofo_list(tcps);
	la_list_del(&(tcps->list));
	test_free(tcps);
	tcp_stream_num--;
}

int handle_tcp_stream_from_cache(struct tcp_stream* tcps)
{
	struct skb_buf *cursor , *tmp;
    list_for_each_entry_safe(cursor, tmp, &tcps->ofo_from_server, list)
    {
    	if(tcps->skb.seq+tcps->skb.data_len == cursor->seq)
    	{
	    	cursor->result=RESULT_HANDLE;
			tcps->callback(cursor);
			tcps->skb.seq = cursor->seq;
			tcps->skb.data_len = cursor->data_len;
			la_list_del(&cursor->list);
	    	test_free(cursor->pload);
			test_free(cursor);
			handle_tcp_stream_from_cache(tcps);
    	}
	}

	return 0;
}

int handle_tcp_stream_from_skb(struct tcp_stream* tcps, struct skb_buf* skb)
{

	//debug_log("old seq %lu len %d----last seq %lu len %d",tcps->skb.seq ,tcps->skb.data_len,skb->seq,skb->data_len);
	//1. common	
	if(tcps->skb.seq+tcps->skb.data_len == skb->seq)
	{
		tcps->skb.seq = skb->seq;
		tcps->skb.data_len = skb->data_len;
		return RESULT_HANDLE;
	}
	//2. out of order
	//2.1 repeat or overlap
	else if(tcps->skb.seq+tcps->skb.data_len > skb->seq)
	{	
		//repeat
		if(tcps->skb.seq==skb->seq && tcps->skb.data_len==skb->data_len)
		{
			skb->pload_len=skb->pload_len-skb->data_len;
		}
		//overlap
		else
		{
			free_tcp_stream(tcps);
		}
		return RESULT_IGNORE;
	}
	//2.2 arrive to early
	else if(tcps->skb.seq+tcps->skb.data_len < skb->seq)
	{
		add2ofo_list(tcps , skb);
		return RESULT_CACHE;
	}

	return RESULT_OTHER;
}

void timeout()
{
	struct tcp_stream *cursor , *tmp;
	 long current_sec;

	if(tcp_stream_num	< MAX_TCP_STREAM_NUN)
		return;
	
	current_sec=get_current_sec();

    list_for_each_entry_safe(cursor, tmp, &tcp_stream_list_head, list)
    {
  		 if(current_sec - cursor->last_time > MAX_TIMEOUT_SEC)
		 {
		 	//debug_log("tcp stream timeout");
			free_tcp_stream(cursor);
		 }
	}
}

void process_tcp(struct skb_buf *skb ,void (*callback)(void*))
{
	struct tcp_stream *new_tcps;
	struct tcp_stream tcps;
	struct iphdr *this_iphdr = (struct iphdr *) (skb->pload);  
	if(this_iphdr->protocol!=IPPROTO_TCP)
    {
    	skb->result=RESULT_IGNORE;
		callback(skb);
    	return;
	}
	//debug_log("tcp_stream_num %d" , tcp_stream_num);
	timeout();
	
	struct tcphdr *this_tcphdr = (struct tcphdr *) (skb->pload+ 4 * this_iphdr->ihl);
	
	skb->data_len = ntohs (this_iphdr->tot_len) -  4 * this_iphdr->ihl - 4 * this_tcphdr->doff;
	if(skb->data_len < 0)
	{
		skb->result=RESULT_IGNORE;
		callback(skb);
    	return;
	}
	if(ntohs (this_iphdr->tot_len)!=skb->pload_len)
	{
		skb->result=RESULT_IGNORE;
		callback(skb);
    	return;
	}
	skb->seq = ntohl(this_tcphdr->seq);
	skb->ack_seq = ntohl(this_tcphdr->ack_seq);
	
	tcps.addr.sip = this_iphdr->saddr;
	tcps.addr.dip = this_iphdr->daddr;
	tcps.addr.sp = this_tcphdr->source;
	tcps.addr.dp = this_tcphdr->dest;

	if((tcps.addr.sip | tcps.addr.dip) == 0)
	{
		skb->result=RESULT_IGNORE;
		callback(skb);
    	return;
	}
	
	tcps.callback = callback;
	//debug_log("syn %d , ack %d rst %d fin %d" , this_tcphdr->syn,this_tcphdr->ack , this_tcphdr->rst , this_tcphdr->fin);	
	new_tcps = find_by_tuple4(&tcps.addr);
	if(!new_tcps)
	{
		//first syn packet from client
		if ((this_tcphdr->syn) && 
			!(this_tcphdr->ack) &&
			!(this_tcphdr->rst))
		{
			tcps.state=TCP_STATE_JUST_EST;
			tcps.from_client=1;
			skb->syn=1;
			new_tcps=new_tcp_stream(&tcps , skb);
		}
		skb->result=RESULT_IGNORE;
		callback(skb);
    	return;
	}
	
	//first syn ack packet from server
	if ((this_tcphdr->syn) && 
			(this_tcphdr->ack))
	{
		tcps.state=TCP_STATE_JUST_EST;
		new_tcps->skb.seq = skb->seq+1;
		new_tcps->skb.ack_seq = skb->ack_seq;
		new_tcps->skb.data_len = 0;
		skb->result=RESULT_IGNORE;
		callback(skb);
		return;
	}
	
	//last fin packet
	if(this_tcphdr->fin||this_tcphdr->rst)	
	{	
		new_tcps->state=TCP_STATE_CLOSE;
		skb->fin=1;
		free_tcp_stream(new_tcps);
		skb->result=RESULT_IGNORE;
		callback(skb);
		return;
	}
	//debug_log("data_len %d from_client %d" , skb->data_len ,new_tcps->from_client);
	if(skb->data_len == 0)
	{
		skb->result=RESULT_IGNORE;
		callback(skb);
    	return;
	}
	
	if(new_tcps->from_client)
	{
		skb->result=RESULT_HANDLE;
		callback(skb);
    	return;
	}

	//data
	if(this_tcphdr->ack)
	{
		new_tcps->state=TCP_STATE_DATA;
		skb->result = handle_tcp_stream_from_skb(new_tcps , skb);
		switch(skb->result)
		{
			case RESULT_HANDLE:
			case RESULT_IGNORE:
				callback(skb);
			case RESULT_CACHE:
			default:
				break;
				//cache to ofo list
		}

		handle_tcp_stream_from_cache(new_tcps);
		
		return;
	}
	else
	{
		skb->result=RESULT_IGNORE;
		callback(skb);
    	return;
	}
	
}

void init_tcp_stream()
{
	INIT_LIST_HEAD(&tcp_stream_list_head);
}
