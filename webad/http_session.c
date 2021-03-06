#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <netinet/in.h>

#include "util.h"
#include "http_session.h"

#define MAX_HTML_SIZE 1024000
#define MAX_HTTP_SESSION_NUN 8
#define MAX_HTTP_SESSION_TIMEOUT_SEC 20
PRIVATE int http_session_num=0;

PRIVATE struct list_head http_session_list_head; 

char* get_data_from_skb(struct skb_buf* skb)
{
	return (char*)(skb->pload + (skb->pload_len - skb->data_len));
}

int get_data_len_from_skb(struct skb_buf* skb)
{
	return skb->data_len;
}

void http_chsum(struct skb_buf* skb)
{
	struct iphdr *this_iphdr = (struct iphdr *) (skb->pload);  
	struct tcphdr *this_tcphdr = (struct tcphdr *) (skb->pload+ 4 * this_iphdr->ihl);
	this_iphdr->tot_len=htons(skb->pload_len);
	this_iphdr->check=ip_chsum(this_iphdr);
	this_tcphdr->check=tcp_chsum(this_iphdr , this_tcphdr , skb->pload_len-4 * this_iphdr->ihl);
}

void change_ip_len(struct skb_buf* skb , unsigned long last_ip_len)
{
	struct iphdr *this_iphdr = (struct iphdr *) (skb->pload);  
	this_iphdr->tot_len=htonl(last_ip_len);
}

void change_seq(struct skb_buf* skb , unsigned long last_seq)
{
	struct iphdr *this_iphdr = (struct iphdr *) (skb->pload);  
	struct tcphdr *this_tcphdr = (struct tcphdr *) (skb->pload+ 4 * this_iphdr->ihl);
	this_tcphdr->seq=htonl(last_seq);
}

int is_html_end(struct http_request* httpr)
{
	char* http_content = get_data_from_skb(httpr->curr_skb);
	int http_content_len = get_data_len_from_skb(httpr->curr_skb);
	
	char* search_data ;

	if(httpr->response_num == 1)
		return 0;
	
	if(httpr->hhdr.res_type==HTTP_RESPONSE_TYPE_CHUNKED)
	{
	 	if(http_content_len<8)
		{
			search_data = http_content;
		}
		else
		{
			search_data = http_content+(http_content_len - 8);
		}
		
		if(strstr(search_data , "0\r\n"))
		{
			return 1;
		}

		if(http_content_len == 1)
		{
			if(search_data[0] == '0')
			{
				return 1;
			}
		}
		
	}
	else if(httpr->hhdr.res_type==HTTP_RESPONSE_TYPE_CONTENTLENGTH)
	{
		if(http_content_len<64)
		{
			search_data = http_content;
		}
		else
		{
			search_data = http_content+(http_content_len - 64);
		}
		
		if(strstr(search_data ,"</html>"))
		{
			return 1;
		}
    }	
	
	return 0;
}
int change_accept_encoding(struct http_hdr* hhdr)
{
	if(hhdr->accept_encoding.l <= 0)
		return ERROR;
	
	if(!strncasecmp(hhdr->accept_encoding.c, "Accept-Encoding: gzip" ,21))
	{
		hhdr->accept_encoding.c[0]='B';
		return OK;
	}

	return ERROR;
}

int http_request_filter(struct http_hdr* hhdr)
{	
	//maybe not http protocal
	if(hhdr->host.l <= 0)
		return ERROR;

	//not html page
	if(hhdr->accept.l <= 0)
		return ERROR;
	
	if(strncasecmp(hhdr->accept.c, "Accept: text/html" ,17)!=0)
	{
		return ERROR;
	}
	return OK;
}

int http_response_filter(struct http_hdr* hhdr)
{	
	//normal page
	if(hhdr->error_code.l <= 0)
		return ERROR;
	if(strncasecmp(hhdr->error_code.c , "HTTP/1.1 200 OK" , 15)!=0)
	{
		return ERROR;
	}
	//not html page
	if(hhdr->content_type.l <= 0)
		return ERROR;
	if(strncasecmp(hhdr->content_type.c, "Content-Type: text/html" ,23)!=0)
	{
		return ERROR;
	}
	return OK;
}

void free_http_request(struct http_request* httpr)
{
	if(!httpr)
		return;
	//debug_log("free_http_request");
	la_list_del(&(httpr->list));
	test_free(httpr);
	http_session_num--;
}

struct http_request* new_http_request(struct tuple4* addr)
{
	struct http_request* new_httpr;
	
	new_httpr=(struct http_request*)test_malloc(sizeof(struct http_request));
	memset(new_httpr, '\0' ,sizeof(struct http_request));
	memcpy(&new_httpr->tcps.addr, addr ,sizeof(struct tuple4));
	new_httpr->tcps.last_time = get_current_sec();
	la_list_add_tail(&(new_httpr->list), &http_session_list_head);
	http_session_num++;
	return new_httpr;
}

struct http_request* find_http_request(struct tuple4* addr , struct skb_buf* skb)
{
	struct http_request *cursor , *tmp;
	list_for_each_entry_safe(cursor, tmp, &http_session_list_head, list)
	{
		if(addr->sip == cursor->tcps.addr.sip &&
			addr->dip== cursor->tcps.addr.dip &&
			addr->sp == cursor->tcps.addr.sp &&
			addr->dp == cursor->tcps.addr.dp
			)
		{
			return cursor;
		}
		else if(addr->sip == cursor->tcps.addr.dip &&
			addr->dip== cursor->tcps.addr.sip &&
			addr->sp == cursor->tcps.addr.dp &&
			addr->dp == cursor->tcps.addr.sp &&
			skb->ack_seq == cursor->tcps.curr_seq + cursor->tcps.curr_data_len)
		{
			return cursor;
		}
		
	}	
	return NULL;
}

int decode_http(struct http_hdr* hhdr, struct skb_buf *skb)
{
	int i = 0;
	char *start,*end;
	char* http_head;
	char* http_data;
	int http_len;

	http_len = skb->data_len;
	http_head = (char*)(skb->pload+(skb->pload_len - skb->data_len));
	if(!http_head)
		return ERROR;

	//////////////http_head_start///////////
	if(!strncasecmp(http_head,"POST ",5))
	{
		hhdr->http_type=HTTP_TYPE_REQUEST_POST;
		return ERROR;
	}
	else if(!strncasecmp(http_head,"GET " ,4))
	{
		hhdr->http_type=HTTP_TYPE_REQUEST_GET;
	}
	else if(!strncasecmp(http_head,"HTTP/1.",7))
	{
			
		hhdr->http_type=HTTP_TYPE_RESPONSE;
	}
	else 
	{
		hhdr->http_type=HTTP_TYPE_OTHER;
		return OK;
	}
	
	end=start=http_head;
	while(i<http_len)
	{	
		if(memcmp(end , "\r\n" , 2)!=0 )
		{
			i++;
			end++;
			continue;
		}

		if(!strncasecmp(start,"GET " ,4))
		{
			new_string(&hhdr->uri, start , end-start);
		}
		else if(!strncasecmp(start,"HTTP/1.",7))
		{
			new_string(&hhdr->error_code , start , end-start);
		}
		else if(!strncasecmp(start,"Host: ",6))
		{
			new_string(&hhdr->host , start , end-start);
		}
		else if(!strncasecmp(start,"Accept-Encoding: ",17))
		{
			new_string(&hhdr->accept_encoding, start , end-start);
		}
		else if(!strncasecmp(start,"Accept: ",8))
		{
			new_string(&hhdr->accept, start , end-start);
		}
		else if(!strncasecmp(start,"User_Agent: ",12))
		{
			new_string(&hhdr->user_agent, start , end-start);
		}
		else if(!strncasecmp(start,"Content-Type: ",14))
		{
			new_string(&hhdr->content_type, start , end-start);
		}
		else if(!strncasecmp(start,"Content_Encoding: ",18))
		{
			new_string(&hhdr->content_encoding, start , end-start);
		}
		else if(!strncasecmp(start,"Content-Length: ",16))
		{
			new_string(&hhdr->content_length, start , end-start);
			hhdr->res_type=HTTP_RESPONSE_TYPE_CONTENTLENGTH;
		}
		else if(!strncasecmp(start,"Transfer-Encoding: chunked",26))
		{
			new_string(&hhdr->transfer_encoding, start , end-start);
			hhdr->res_type=HTTP_RESPONSE_TYPE_CHUNKED;
		}
		else if(!strncasecmp(start,"Transfer-Encoding:  chunked",27))
		{
			new_string(&hhdr->transfer_encoding, start , end-start);
			hhdr->res_type=HTTP_RESPONSE_TYPE_CHUNKED;
		}
		i+=2;
		end+=2;
		start=end;
		if(!memcmp(start , "\r\n" , 2))
		{
			start+=2;
			break;
		}
		
	}

	//////////////http_head_end///////////
	http_data=start;
	if(!http_data)
	{
		return ERROR;
	}
	
	//////////////include /r/n/r/n ///////
	hhdr->httph_len = http_data - http_head;
	
	return OK;
}

void http_timeout()
{
	struct http_request *cursor , *tmp;
	long current_sec;

	//debug_log("%d" , http_session_num);
	if(http_session_num	< MAX_HTTP_SESSION_NUN)
		return;
	
	current_sec=get_current_sec();

    list_for_each_entry_safe(cursor, tmp, &http_session_list_head, list)
    {
  		 if(current_sec - cursor->tcps.last_time > MAX_HTTP_SESSION_TIMEOUT_SEC)
		 {
		 	//debug_log("http session timeout");
			free_http_request(cursor);
		 }
	}
}

void process_http(struct skb_buf *skb ,void (*callback)(void*))
{
	struct http_request* new_httpr;
	struct http_request tmp_httpr;
	struct tuple4 addr;
	
	struct iphdr *this_iphdr = (struct iphdr *) (skb->pload);  
	struct tcphdr *this_tcphdr = (struct tcphdr *) (skb->pload+ 4 * this_iphdr->ihl);
	
	addr.sip = this_iphdr->saddr;
	addr.dip = this_iphdr->daddr;
	addr.sp = this_tcphdr->source;
	addr.dp = this_tcphdr->dest;
	
	http_timeout();

	switch(skb->result)
	{
		case RESULT_FROM_CLIENT:
			{
				new_httpr = find_http_request(&addr ,skb);
				//first request packet must get
				if(!new_httpr)
				{
					new_httpr = new_http_request(&addr);
					if(ERROR == decode_http(&new_httpr->hhdr, skb))
					{
						goto result_ignore;
					}

					if(new_httpr->hhdr.http_type != HTTP_TYPE_REQUEST_GET)
					{
						goto result_ignore;
					}
					
					if(ERROR == http_request_filter(&new_httpr->hhdr))
					{
						goto result_ignore;
					}
					if(ERROR == change_accept_encoding(&new_httpr->hhdr))
					{
						goto result_ignore;
					}
					
					goto result_client;
					
				}
				
				//repeat session request
				//debug_log("repeat");
				goto result_ignore;
			}
		case RESULT_FROM_SERVER:
			{
				new_httpr = find_http_request(&addr ,skb);
				//no find
				if(!new_httpr)
				{
					goto result_ignore;
				}
			
				//first response packet must have http head
				if(new_httpr->hhdr.http_type == HTTP_TYPE_REQUEST_GET)
				{
					if(ERROR == decode_http(&new_httpr->hhdr , skb))
					{
						goto result_ignore;
					}
					if(new_httpr->hhdr.http_type != HTTP_TYPE_RESPONSE)
					{
						goto result_ignore;
					}

					if(ERROR == http_response_filter(&new_httpr->hhdr))
					{
						goto result_ignore;
					}

					goto result_server;
				}
				//other response packet no need decode http head
				else if(new_httpr->hhdr.http_type == HTTP_TYPE_RESPONSE)
				{
					goto result_server;
				}
				else
				{
					goto result_ignore;
				}
				break;
			}
			
		default:break;	
	}

	result_client:
		new_httpr->tcps.curr_seq = skb->seq ;
		new_httpr->tcps.curr_data_len = skb->data_len;
		new_httpr->curr_skb = skb;
		callback(new_httpr);
		http_chsum(skb);
		new_httpr->curr_skb->result=RESULT_IGNORE;
		callback(new_httpr);
		return;
	result_server:
		new_httpr->response_num++;
		new_httpr->curr_skb = skb;
		callback(new_httpr);
		http_chsum(skb);
		new_httpr->curr_skb->result=RESULT_IGNORE;
		callback(new_httpr);
		if(is_html_end(new_httpr))
		{
			//debug_log("end %s" , get_data_from_skb(new_httpr->curr_skb));
			free_http_request(new_httpr);
		}
		return;
	result_ignore:
		free_http_request(new_httpr);
		skb->result=RESULT_IGNORE;
		tmp_httpr.curr_skb = skb;
		callback(&tmp_httpr);
		return;
}

void init_http_session()
{
	INIT_LIST_HEAD(&http_session_list_head);
}

