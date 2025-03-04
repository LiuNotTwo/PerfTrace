#include "./lib/udp_owamp.h"
#include "./lib/err_handle.c"
#include "./lib/wrapunix.c"
#include "./lib/udp_connect.c"
#include "./lib/func.c"
#include "./lib/udp_server_reuseaddr.c"

int					listenfd, n_tcp, n_udp, connfd, sockfd_udp, pkt_cnt, RTT;
long				RAND_ID;
pid_t				childpid;
socklen_t			clilen_tcp, clilen_udp;
struct sockaddr_in	servaddr, cliaddr;
struct sockaddr_storage	cliaddr_udp;
char				buff_tcp[MAXLINE], buff_udp[MAXLINE];
struct OWD_Record		OWDS[MAXPKT2+1];


void * response1(void * send_sd){
	struct Probe_Pkt * probe_pkt = buff_udp;
	struct Reply_Pkt * reply_pkt = buff_udp;
	struct timeval tv;
	pkt_cnt = 0;
	int sd = *(int *) send_sd;
	for ( ; ; ) {
		clilen_udp = sizeof(cliaddr_udp);
		n_udp = Recvfrom(sd, buff_udp, MAXLINE, 0, (SA *)&cliaddr_udp, &clilen_udp);
		if(RAND_ID == probe_pkt->ID){
			pkt_cnt ++;
			gettimeofday(&tv, NULL);
			reply_pkt->Send_arrive_time = tv.tv_sec * 1000000 + tv.tv_usec;
			reply_pkt->RSN = pkt_cnt;
			Sendto(sd, buff_udp, sizeof(struct Reply_Pkt), 0, (SA *)&cliaddr_udp, clilen_udp);
		}
	}
}

void * response2(void * send_sd){
	struct Probe_Pkt * probe_pkt = buff_udp;
	struct timeval tv;
	pkt_cnt = 0;
	int sd = *(int *) send_sd;
	for ( ; ; ) {
		clilen_udp = sizeof(cliaddr_udp);
		n_udp = Recvfrom(sd, buff_udp, MAXLINE, 0, (SA *)&cliaddr_udp, &clilen_udp);
		if(RAND_ID == probe_pkt->ID){
			pkt_cnt ++;
			gettimeofday(&tv, NULL);
			OWDS[pkt_cnt].send_time = probe_pkt->Send_time;
			OWDS[pkt_cnt].SSN = probe_pkt->SSN;
			OWDS[pkt_cnt].OWD = (int)(tv.tv_sec * 1000000 + tv.tv_usec - probe_pkt->Send_time);
		}
	}
}

int main(int argc, char **argv)
{
	//int pageSize = sysconf(_SC_PAGE_SIZE);
	memset(OWDS, 0, sizeof(OWDS));
	void *shared_addr = mmap(NULL, sizeof(int)*10, PROT_READ | PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);
	int *isOccupied = 	shared_addr;
	isOccupied[0]   = 	0;
	void				sig_chld(int);

	listenfd = Socket(AF_INET, SOCK_STREAM, 0);

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family      = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port        = htons(19999);	/* TCP server */

	Bind(listenfd, (SA *) &servaddr, sizeof(servaddr));

	Listen(listenfd, LISTENQ);

	for ( ; ; ) {
		clilen_tcp = sizeof(cliaddr);
		if ( (connfd = accept(listenfd, (SA *) &cliaddr, &clilen_tcp)) < 0) {
			if (errno == EINTR)
				continue;		/* back to for() */
			else
				err_sys("accept error");
		}

		if ( (childpid = Fork()) == 0) {	/* child process */
			Close(listenfd);	/* close listening socket */
			if (isOccupied[0] == 0){
				isOccupied[0] = 1;
				struct timeval tv1, tv2;
				gettimeofday(&tv1, NULL);
				snprintf(buff_tcp, sizeof(buff_tcp), ACCEPT);
				Write(connfd, buff_tcp, strlen(buff_tcp));
				if ((n_tcp = Read(connfd, buff_tcp, MAXLINE)) > 0){
					gettimeofday(&tv2, NULL);
					RTT = (int)((tv2.tv_sec-tv1.tv_sec)*1000000 + tv2.tv_usec-tv1.tv_usec);
					struct Task_Meta *	task_metadata = buff_tcp;
					int duration = task_metadata->duration;
					printf("Task Info:\n");
					if(task_metadata->task_mode==1){
						printf("ID=%ld\tmode=%d\tRTT=%dus\tpkt_num=%d\tinterval=%dus\n", task_metadata->ID, task_metadata->task_mode, RTT, task_metadata->pkt_num, task_metadata->interval);
					}else if(task_metadata->task_mode==2){
						printf("ID=%ld\tmode=%d\tRTT=%dus\tduration=%dus\n", task_metadata->ID, task_metadata->task_mode, RTT, task_metadata->duration);
					}else{
						printf("ID=%ld\tmode=%d\tRTT=%dus\n", task_metadata->ID, task_metadata->task_mode, RTT);
					}
					printf("\n");
					RAND_ID = task_metadata->ID;
					sockfd_udp = Udp_server_reuseaddr(NULL, "19999", NULL);
					int nRecvBuf=1024*1024; //set 32KB rcv buff
					setsockopt(sockfd_udp, SOL_SOCKET, SO_RCVBUF,(const char*)&nRecvBuf,sizeof(int));
					if(task_metadata->task_mode == 1){
						pthread_t response1_t;
						pthread_create(&response1_t, NULL, response1, &sockfd_udp);
						//printf("I'm OK. Please send packet\n");
						snprintf(buff_tcp, sizeof(buff_tcp), "I'm OK. Please send packet\n");
						Write(connfd, buff_tcp, strlen(buff_tcp));
						us_sleep(duration + RTT + 100000);
						// Kill the sending thread and receiving thread
						pthread_cancel(response1_t);

						int *pkt_cnt_received = buff_tcp;
						pkt_cnt_received[0] = pkt_cnt;
						//pkt_cnt_tmp[1] = 0;
						Write(connfd, buff_tcp, sizeof(int)); // send the number of received pkt.
					}else{
						pthread_t response2_t;
						pthread_create(&response2_t, NULL, response2, &sockfd_udp);
						//printf("I'm OK. Please send packet\n");
						snprintf(buff_tcp, sizeof(buff_tcp), "I'm OK. Please send packet\n");
						Write(connfd, buff_tcp, strlen(buff_tcp));
						while((n_tcp = Read(connfd, buff_tcp, MAXLINE)) > 0){
							pkt_cnt = 0;
							buff_tcp[n_tcp] = 0;	/* null terminate */
							if(strcmp(buff_tcp, MODE2_MEASURE) == 0){
								if((n_tcp = Read(connfd, buff_tcp, MAXLINE)) > 0){
									struct Mode2_Send_Meta *Sendmeta_tcp = buff_tcp;
									duration = Sendmeta_tcp->duration;
									long owd_sum = 0;
									int pkt_num_send = Sendmeta_tcp->pkt_num_send;
									int psize = Sendmeta_tcp->psize;
									int i;
									long min_rcv_time = OWDS[1].send_time + OWDS[1].OWD;
									long max_rcv_time = OWDS[pkt_cnt].send_time + OWDS[pkt_cnt].OWD;
									int pkt_num_rcv_in_duration = 0;
									struct Mode2_Result *Result_tcp = buff_tcp;
									Result_tcp->jitter = 0.0;
									if(pkt_cnt > 1){
										Result_tcp->jitter = abs(OWDS[2].OWD - OWDS[1].OWD);
									}
									for(i=1; i<pkt_cnt+1; i++){
										owd_sum += OWDS[i].OWD;
										if(OWDS[i].send_time + (long)OWDS[i].OWD - min_rcv_time <= duration){
											pkt_num_rcv_in_duration ++;
										}
										if(i>2){
											Result_tcp->jitter = Result_tcp->jitter + ((abs(OWDS[i].OWD - OWDS[i-1].OWD)) - Result_tcp->jitter)/16.0;
										}
									}
									Result_tcp->arv_pkt_cnt = pkt_cnt;
									Result_tcp->loss_rate = (float)(pkt_num_send - pkt_cnt)/pkt_num_send;
									Result_tcp->rate1 = (float)pkt_cnt*psize/(max_rcv_time-min_rcv_time)*1000000*8;
									Result_tcp->rate2 = (float)pkt_num_rcv_in_duration*psize/duration*1000000*8;
									Result_tcp->aver_owd = (int)(owd_sum / pkt_cnt);
									Write(connfd, buff_tcp, sizeof(struct Mode2_Result));
								}
								/*us_sleep(duration + RTT + 2000);
								struct Mode2_Reply_Header * head_tcp = buff_tcp;
								struct OWD_Record * payload_tcp = buff_tcp + sizeof(struct Mode2_Reply_Header);
								int payload_record_num = (MAXPSIZE - sizeof(struct Mode2_Reply_Header)) / sizeof(struct OWD_Record);
								int i,offset = 0;
								while(offset < pkt_cnt){
									head_tcp->size = min(payload_record_num, pkt_cnt - offset);
									head_tcp->ID = RAND_ID;
									head_tcp->offset = offset+1;
									head_tcp->pkt_tot = pkt_cnt;
									for(i=0; i<(head_tcp->size); i++){
										offset ++;
										payload_tcp[i].OWD = OWDS[offset].OWD;
										payload_tcp[i].SSN = OWDS[offset].SSN;
										//printf("SSN=%d, RSN=%d, OWD=%d\n", payload_tcp[i].SSN, offset, payload_tcp[i].OWD);
									}
									Write(connfd, buff_tcp, sizeof(struct Mode2_Reply_Header) + sizeof(struct OWD_Record)*(head_tcp->size));
									printf("offset=%d\n", offset);
									us_sleep(3000);
								}
								*/
								memset(OWDS, 0, sizeof(OWDS));
								pkt_cnt = 0;
							}else break;
						}
						// Kill the sending thread and receiving thread
						pthread_cancel(response2_t);
						


					}
					close(sockfd_udp);
				}
				isOccupied[0] = 0;
			}else{
				snprintf(buff_tcp, sizeof(buff_tcp), DENY);
				Write(connfd, buff_tcp, strlen(buff_tcp));
			}
			exit(0);
		}
		signal(SIGCHLD, SIG_IGN);
		Close(connfd);			/* parent closes connected socket */
	}
}
