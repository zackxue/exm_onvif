/** @file	       net_aplay.c
 *   @brief 	���ղ������ͻ��˷�������Ƶ�������������
 *   @date 	2007.04
 */
#include "rtimage2.h"

#include <commonlib.h>
#include <gtthread.h>
#include <netinfo.h>    
#include <gt_com_api.h>
#include <audiofmt.h>
#include "rtnet_cmd.h"
#include <gt_errlist.h>
#include <gtsocket.h>
#include <fcntl.h>
//#include "common/sample_comm.h" //zsk add

#define AUDIO_PLAY_PKT_SIZE     320    //2048 zsk mod
#define AAC_HEAD_SIZE           4
#define MAX_AAC_PKT_SIZE		4096
#define TW2865_FILE     "/dev/tw2865dev" 
#define AUDIO_ADPCM_TYPE ADPCM_TYPE_DVI4/* ADPCM_TYPE_IMA, ADPCM_TYPE_DVI4*/
#define AUDIO_AAC_TYPE AAC_TYPE_AACLC   /* AAC_TYPE_AACLC, AAC_TYPE_EAAC, AAC_TYPE_EAACPLUS*/
#define G726_BPS MEDIA_G726_16K         /* MEDIA_G726_16K, MEDIA_G726_24K ... */
#define AMR_FORMAT AMR_FORMAT_MMS       /* AMR_FORMAT_MMS, AMR_FORMAT_IF1, AMR_FORMAT_IF2*/
#define AMR_MODE AMR_MODE_MR74         /* AMR_MODE_MR122, AMR_MODE_MR102 ... */
#define AMR_DTX 0
extern unsigned long GetTickCount();
extern int write_rawaudio2file_send();
/*
typedef struct tagSAMPLE_AIAENC_SINGLE_S
{
    HI_S32 s32AencChn; 
    FILE *pfd;
    HI_S32 u32GetStrmCnt;
} SAMPLE_AIAENC_SINGLE_S;

typedef struct tagSAMPLE_AIAENC_S
{
    HI_BOOL bStart;
    pthread_t stAencPid;
    HI_U32 u32AeChnCnt;
    HI_S32 s32BindAeChn;    
    SAMPLE_AIAENC_SINGLE_S astAencChn[AENC_MAX_CHN_NUM];
} SAMPLE_AIAENC_S;

typedef struct tagSAMPLE_AOADEC_S
{
    HI_BOOL bStart;
    HI_S32 AdChn; 
    FILE *pfd;
    pthread_t stAdPid;
} SAMPLE_AOADEC_S;


static SAMPLE_AIAENC_S s_stSampleAiAenc;
static SAMPLE_AOADEC_S s_stSampleAoAdec;
static PAYLOAD_TYPE_E s_enPayloadType = PT_G711U;

static HI_BOOL s_bAioReSample = HI_FALSE;
static AUDIO_RESAMPLE_ATTR_S s_stAiReSmpAttr;
static AUDIO_RESAMPLE_ATTR_S s_stAoReSmpAttr;
*/

/**   
	*	��������ʱ��ļ�����õ�ʱ���	 
	*	@param	 struct   timeval*	 resule   ���ؼ��������ʱ��   
	*	@param	 struct   timeval*	 x			   ��Ҫ�����ǰһ��ʱ��   
	*	@param	 struct   timeval*	 y			   ��Ҫ����ĺ�һ��ʱ��   
	*	return	 -1   failure	,0	 success   
**/   
int   timeval_subtract(struct	timeval*   result,	 struct   timeval*	 x,   struct   timeval*   y)   
{	

	  if(x->tv_sec > y->tv_sec)   
		  return   -1;	 
  
	  if((x->tv_sec == y->tv_sec) && (x->tv_usec > y->tv_usec))   
		  return   -1;	 
  
	  result->tv_sec = (y->tv_sec-x->tv_sec);	
	  result->tv_usec = (y->tv_usec-x->tv_usec);   
  
	  if(result->tv_usec < 0)	
	  {   
		  result->tv_sec--;   
		  result->tv_usec += 1000000;	
	  }   
  
	  return   0;	
}	

/** 
 *   @brief     ��ʼ����������Ƶ���з����ȫ���û�����
 *   @return   0��ʾ�ɹ�,��ֵ��ʾʧ��
 */ 
static int init_net_aplay(void)
{
    tcprtimg_svr_t  *p=get_rtimg_para();
    aplay_server_t *svr=&p->aplay_server;
    aplay_usr_t      *usr=NULL;
    int                   i;
    pthread_mutex_init(&svr->l_mutex,NULL);
    pthread_mutex_init(&svr->s_mutex,NULL);         
    svr->listen_fd=-1;
    
    ///�û���Ϣ��ʼ��
    for(i=0;i<(TCPRTIMG_MAX_APLAY_NO+1);i++)
    {
        usr=&svr->usr_list[i];
        memset((void*)usr,0,sizeof(aplay_usr_t));
        pthread_mutex_init(&usr->u_mutex,NULL);
        usr->fd=-1;
        usr->no=i;
        usr->pid=-1;
        usr->thread_id=-1;
        usr->magic=0x55aa;                                  ///<�ѳ�ʼ����־
        
    }
   
   	return 0;
}

/** 
 *   @brief     ��ʼ������Ƶ�û����ݽṹ(�����������ӵ���ʱ)
 *   @param  usr �û���Ϣ�ṹָ��
 *   @return   ��
 */     
static void init_aplay_usr_info(IO aplay_usr_t *usr)
{
    tcprtimg_svr_t *p=get_rtimg_para();

    pthread_mutex_lock(&usr->u_mutex);
    
    memset(&usr->addr,0,sizeof(struct sockaddr_in));
    usr->fd=-1;
    usr->th_timeout=p->th_timeout;
    usr->timeout_cnt=0;
    gettimeofday(&usr->start_time,NULL);
    memset(&usr->last_cmd_time,0,sizeof(struct timeval));
    sprintf(usr->name,"%s","newuser");
    usr->serv_stat=0;
    usr->aenc_no=-1;
    memset(&usr->sock_attr,0,sizeof(socket_attrib_t));
    memset(&usr->recv_info,0,sizeof(stream_recv_info_t));
    pthread_mutex_unlock(&usr->u_mutex);
}

/** 
 *   @brief     ��һ���û���ַ��Ϣ�����û��б�
 *   @param  usr ����û���Ϣ�Ľṹָ��
 *   @param  guest ���û���ip��ַ�Ͷ˿ں���Ϣ
 *   @return   0��ʾ�ɹ� 1��ʾæ ��ֵ��ʾ����
 *   @warnning ���ô˺���ǰ������Ӧ���Ѿ�ȡ����ý������s_mutex
 */ 
static int add_aplay_usr_list(aplay_usr_t *usr,struct sockaddr_in *guest)
{
    tcprtimg_svr_t      *p=get_rtimg_para();
    aplay_server_t     *svr=&p->aplay_server;
    if((usr==NULL)||(guest==NULL))
        return -EINVAL;       
    svr->usrs++;
    if(svr->usrs>p->max_aplay_usrs)
    {
        svr->usrs--;
        return 1;
    }
    memcpy(&usr->addr,guest,sizeof(struct sockaddr_in));    ///<�����û���Ϣ
    return 0;
    
}
/** 
 *   @brief     ����Ƶ���з�����ɾ��ָ�����û�
 *   @param  usr ����û���Ϣ�Ľṹָ��
 *   @return   0��ʾ�ɹ�  ��ֵ��ʾ����
 *   @warnning ���ô˺���ǰ������Ӧ���Ѿ�ȡ����ý������s_mutex
 */ 
static int del_aplay_usr_list(aplay_usr_t *usr)
{
    tcprtimg_svr_t      *p=get_rtimg_para();
    aplay_server_t     *svr=&p->aplay_server;
    if(usr==NULL)
        return -EINVAL;
    svr->usrs--;
    if(svr->usrs<0)
    {
        showbug();
        logbug();
    }
    

    ///ɾ���û���Ϣ
    pthread_mutex_lock(&usr->u_mutex);
    usr->fd=-1;
    usr->serv_stat=-1;
    usr->aenc_no=-1;
    pthread_mutex_unlock(&usr->u_mutex);
    return 0;
}

/** 
 *   @brief     ��������Ƶ���з������Ӧ��Ϣ
 *   @param  fd Ŀ��������
 *   @param  result ���صĴ������
 *   @param  ��Ҫ���ڷ�����Ϣanswer_data�������Ϣ
 *   @param  datalen buf����Ч���ݵĸ���
 *   @return   ��ֵ��ʾ����,�Ǹ���ʾ�ɹ�
 */ 
int send_aplay_ack_pkt(int fd,WORD result,char* buf,int datalen)
{
	int rc;
	DWORD	sendbuf[100];
	struct gt_usr_cmd_struct *cmd;
	struct gt_pkt_struct *send;
	struct viewer_subscribe_answer_audio_struct *answer;
	int answerlen;
	
	if(fd<0)
		return -1;
	send=(struct gt_pkt_struct *)sendbuf;
	cmd=(struct gt_usr_cmd_struct*)send->msg;
	cmd->cmd=VIEWER_SUBSCRIBE_ANSWER_AUDIO;
	cmd->en_ack=0;
	cmd->reserve0=0;
	cmd->reserve1=0;
	answer=(struct viewer_subscribe_answer_audio_struct  *)cmd->para;
	rc=get_devid(answer->dev_id);
	answer->status=result;
	rc=1;
	memcpy((BYTE*)&answer->audio_trans_id,(BYTE*)&rc,4);
	if(buf!=NULL)
	{
			answerlen=datalen;
			memcpy(answer->answer_data,buf,answerlen);
	}
	else
	{
		switch(result)
		{
			case 0:
				
			break;
			case ERR_DVC_BUSY:	
				answerlen=0;
			break;
			default:
				answerlen=0;
			break;
		}
	}
	answer->answer_data_len=answerlen;
	cmd->len=sizeof(struct viewer_subscribe_answer_audio_struct)-sizeof(answer->answer_data)+answerlen+6;	
	rc=gt_cmd_send_noencrypt(fd,send,cmd->len+2,NULL,0);

	return rc;
}



/** 
 *   @brief     ���������û���������Ƶ���ж�������
 *   @param  usr �յ�������û��ṹָ��
 *   @param  cmd �������յ�������ṹָ��
 *   @return   0��ʾ���غ����ִ��,��ֵ��ʾ���غ���Ҫ�Ͽ�����
 */ 
static int proc_audio_subscrib_cmd(IO aplay_usr_t *usr,IN struct gt_usr_cmd_struct* cmd)
{
    char                 *ptemp=NULL;
    int                    ret;
    int                   *sample_rate=NULL;
	tcprtimg_svr_t *p=get_rtimg_para();
    struct viewer_subscribe_audio_cmd_struct *subscribe=(struct viewer_subscribe_audio_cmd_struct*)cmd->para;
    if(cmd->cmd!=VIEWER_SUBSCRIBE_AUDIO)
        return -EINVAL;

    sample_rate=(int*)subscribe->audioheader_data;
    printf     ("�յ� %s ����Ƶ���ж�������account=%s type=%d(%s) sample_rate=%d\n",inet_ntoa(usr->addr.sin_addr),subscribe->account,subscribe->audiotype,get_audio_fmt_str(subscribe->audiotype),*sample_rate);
    gtloginfo("�յ� %s ����Ƶ���ж�������account=%s type=%d(%s) sample_rate=%d\n",inet_ntoa(usr->addr.sin_addr),subscribe->account,subscribe->audiotype,get_audio_fmt_str(subscribe->audiotype),*sample_rate);




    pthread_mutex_lock(&usr->u_mutex);
	usr->serv_stat=1;
    usr->a_sam_rate=*sample_rate;                           ///<��Ƶ������
    switch(subscribe->audiotype) 
    {
    	case AUDIO_PLAY_TYPE_PCM:
    	case AUDIO_PLAY_TYPE_UPCM:
		case AUDIO_PLAY_TYPE_AAC:
			usr->a_fmt=subscribe->audiotype;
			break;
    	default:
    		usr->a_fmt=AUDIO_PLAY_TYPE_UNKNOW;
			break;
    }
    if((usr->a_sam_rate<7500)||(usr->a_sam_rate>8500))
        usr->a_sam_rate=8000;                                   ///<����������Χ
    strncpy(usr->name,(char *)subscribe->account,20);   ///<�û����Ϊ19
    usr->name[19]='\0';
    ptemp=strchr(usr->name,'\n');
    if(ptemp!=NULL)
    {///ȥ���س���
        *ptemp='\0';
    }
    pthread_mutex_unlock(&usr->u_mutex);

	if(usr->aenc_no!=-1)//�û��Ѿ��ڲ���,����ʧ��
		goto NOT_SUPPORT;
	//if(��ȡ�����û��Ƿ��ڲ���subscribe->audio_channel)
	//	goto FAIL;
	if(get_playback_stat(subscribe->audio_channel)!=-1)
	{
		printf("get_playback_stat   %d\n",get_playback_stat(subscribe->audio_channel));
	
		mutichannel_set_playback_to_live(subscribe->audio_channel);
		gtloginfo("ͨ��[%d]����¼��ط�״̬���յ���Ƶ��������׼������¼��ط�״̬\n",subscribe->audio_channel);
	}
	//ret=send_audio_channel2main(subscribe->audio_channel);

	
	ret=send_down_audio2enc(subscribe->audio_channel,0);

	if(ret==3)	
		goto BUSY;
	else if(ret==2)
		goto NOT_SUPPORT;
	else if(ret==4)
		goto SUBSCRIBE_FAIL;

	send_aplay_ack_pkt(usr->fd,RESULT_SUCCESS,"success",strlen("success")+1);
	usr->aenc_no=subscribe->audio_channel;
	return 0;
BUSY:
	send_aplay_ack_pkt(usr->fd,ERR_DVC_BUSY,"fail already play",strlen("fail already play\n")+1);
	return -1;
NOT_SUPPORT:
	send_aplay_ack_pkt(usr->fd,ERR_DVC_NO_AUDIO,"fail no audio",strlen("fail no audio\n")+1);
	printf(" NOT SUPPORT!\n");
	return -2;
SUBSCRIBE_FAIL:
	send_aplay_ack_pkt(usr->fd,ERR_DVC_INTERNAL,"fail subscribe",strlen("fail subscribe\n")+1);
	return -3;




	

}
/** 
 *   @brief     ���������û���������Ƶ��������
 *   @param  usr �յ�������û��ṹָ��
 *   @param  cmd �������յ�������ṹָ��
 *   @return   0��ʾ���غ����ִ��,��ֵ��ʾ���غ���Ҫ�Ͽ�����
 */ 
static int process_net_aplay_cmd(aplay_usr_t *usr,struct gt_usr_cmd_struct* cmd)
{
    int                    ret=0;

    if((usr==NULL)||(cmd==NULL))
        return -EINVAL;

    gettimeofday(&usr->last_cmd_time,NULL);                                 ///<���յ������ʱ��
    switch(cmd->cmd)
    {
        case VIEWER_SUBSCRIBE_AUDIO:
            ret=proc_audio_subscrib_cmd(usr,cmd);
        break;
        case VIEWER_SUBSCRIBE_AUDIO_START:
            ///���ڲ�������,ֱ���ӵ�(������)
        break;
        default:
            printf("rtimage recv unknow net snd cmd:0x%04x\n",cmd->cmd);
        break;        
    }
    
    return ret;
}




/** 
 *   @brief       �������緢������Ƶ����,���뻺����
 *   @param    fd �Ѿ����ӵ�����������
 *   @buf         buf ׼��������ݵĻ�����
 *   @read_len ׼����ȡ�����ݳ���
 *   @return     ��ֵ��ʾ���յ����ֽ���,��ֵ��ʾ����
 */
static inline int read_net_audio_data(int fd,void *buf,int read_len)
{
    return fd_read_buf(fd,buf,read_len);
}




/** 
 *   @brief     ���ղ����������û�����Ƶ���з�����߳�
 *   @return   0��ʾ�ɹ�,��ֵ��ʾʧ��
 */ 
static void *rtnet_aplay_thread(void *para)
{
    static  const char        proto_head[4]={0x40,0x55,0xaa,0x55};   ///<�ϵ�Э������ҪVIEWER_SUBSCRIBE_AUDIO_START����
    static  const char        proto_start_len = 36;                             ///<VIEWER_SUBSCRIBE_AUDIO_START���������������
    char                      *pbuf=NULL;                                            ///<����ָ��
    tcprtimg_svr_t           *p=get_rtimg_para();
    aplay_server_t          *svr=&p->aplay_server;
    aplay_usr_t               *usr=(aplay_usr_t*)para;
	media_source_t 			* adec= (media_source_t*)get_audio_dec(0);
	int i;
	int *i_pbuf;
	for(i=0;i<MAX_AUDIO_DECODER;i++)

	{
		adec->attrib->stat=ENC_STAT_OK;
		adec++;
	}

    struct sockaddr_in     guest_addr;
    fd_set                        read_fds;
    int                             accept_fd=-1;
    unsigned int                             addrlen;
    int                             ret,ret2;
    int                             sel;
	int u1=0;
    struct timeval	         timeout;
    char             net_rec_buf[4096+36];		                ///<�������緢�������ݵĻ�����
    int                             read_size=0;                                  ///<һ�δ������ȡ���ֽ���
    int                             first_audio_flag=1;                        ///<��һ�β�����Ƶ��־

	struct   timeval   start,stop,diff;
    struct gt_pkt_struct* recv=(struct gt_pkt_struct*)net_rec_buf;
    if(usr==NULL)
    {
        printf     ("rtnet_aplay_thread para=NULL exit thread!!\n");
        gtloginfo("rtnet_aplay_thread para=NULL exit thread!!\n");
        pthread_exit(NULL);
    }
    printf     (" start rtnet_aplay_thread user:%d...\n",usr->no);
    gtloginfo(" start rtnet_aplay_thread user:%d...\n",usr->no);
    while(1)
    {
        ///��������
        FD_ZERO(&read_fds);
        addrlen=sizeof(struct sockaddr_in);

        if(pthread_mutex_lock(&svr->l_mutex)==0)
        {
        	accept_fd = accept(svr->listen_fd, (void*)&guest_addr, &addrlen);     ///<����������    		
        }
        else
            continue;
        if((accept_fd<0)&&(errno==EINTR)) 
        {
            pthread_mutex_unlock(&svr->l_mutex);
            continue;
        }
         else if(accept_fd<0) 
        { 
        	printf("rtnet_aplay_thread accept errno=%d:%s!!\n",errno,strerror(errno));
        	gtlogerr("rtnet_aplay_thread accept errno=%d:%s!!\n",errno,strerror(errno));
        	pthread_mutex_unlock(&svr->l_mutex);
         	continue; 
        } 
		 
	  ///�������Ƶ�豸������Ӧ����
        ret = get_audio_num();
		if(ret <= 0)
		{
			send_aplay_ack_pkt(accept_fd,ERR_EVC_NOT_SUPPORT,"device not support audio\n",strlen("device not support audio\n")+1);
			continue;
		}
	 
        printf     ("come a new net_aplay guest:%s \n",inet_ntoa(guest_addr.sin_addr));
        gtloginfo("come a new net_aplay guest:%s \n",inet_ntoa(guest_addr.sin_addr));
        init_aplay_usr_info(usr);
        #if 0
            TODO:check valid guest
        #endif
         pthread_mutex_lock(&svr->s_mutex);
         ret=add_aplay_usr_list(usr,&guest_addr);
         pthread_mutex_unlock(&svr->s_mutex);
         
         pthread_mutex_unlock(&svr->l_mutex);
         
         net_set_linger(accept_fd,0);
        if(ret!=0)
        {
            { ///æ
                send_aplay_ack_pkt(accept_fd,ERR_DVC_BUSY,"device busy\n",strlen("device busy\n")+1);
                sleep(1);
                close(accept_fd);
                printf     ("�豸æ,�޷����%s����Ƶ���з���ret=%d!\n",inet_ntoa(guest_addr.sin_addr),ret);
                gtloginfo("�豸æ,�޷����%s����Ƶ���з���ret=%d!\n",inet_ntoa(guest_addr.sin_addr),ret);
                
                continue;
            }
            
        }
		
       ///����socket����
        usr->fd=accept_fd; 
        usr->sock_attr.send_buf_len=net_get_tcp_sendbuf_len(usr->fd);
        usr->sock_attr.recv_buf_len=net_get_tcp_recvbuf_len(usr->fd);
        
        ret=net_activate_keepalive(usr->fd);
        ret=net_set_recv_timeout(usr->fd,3);
        ret=net_set_nodelay(usr->fd);        
        ret=net_set_linger(usr->fd,0);

        printf     ("tcprtimage �ɹ�������һ����Ƶ���з�������(uno:%d,total:%d,max:%d):%s\n",
            usr->no,svr->usrs,p->max_aplay_usrs,inet_ntoa(usr->addr.sin_addr));
        gtloginfo ("tcprtimage �ɹ�������һ����Ƶ���з�������(uno:%d,total:%d,max:%d):%s\n",
            usr->no,svr->usrs,p->max_aplay_usrs,inet_ntoa(usr->addr.sin_addr));


        while(1)
        {
            if((usr->fd>0)&&(accept_fd>0))
            {
                FD_SET(accept_fd,&read_fds);
            }
            else
            {
                break;//�Ѿ����Ͽ�
            }
            if(usr->serv_stat<0)
            {
                break;
            }
            timeout.tv_sec=5;
            timeout.tv_usec=0;
            sel=select(accept_fd+1,&read_fds,NULL,NULL,&timeout);
         	if(sel==0)
            {

                //continue;
    	 	 	 printf("select timeout \n");
    	 	 	 break;
            }
            else if(sel<0)
            {
                if(errno!=EINTR)
                    sleep(1);
                continue;
            }
            if(usr->fd<0)
                break;
            if(FD_ISSET(accept_fd,&read_fds))
            {
            	
                if(usr->serv_stat<=0)
                {///��������״̬
                    ret=gt_cmd_pkt_recv(accept_fd,recv,sizeof(net_rec_buf),NULL,0);		
					if(ret>=0)
                    {		
                        pthread_mutex_lock(&usr->u_mutex);
                        usr->timeout_cnt=0;     ///<�����ʱ������
                        pthread_mutex_unlock(&usr->u_mutex);
						//���ﴦ�����ֻ�ܽ���ip�Խ��豸��
                        ret=process_net_aplay_cmd(usr,(struct gt_usr_cmd_struct*)( recv->msg));
                        if(ret<0)
						{
                        	    break;
							}
                         if(usr->a_fmt==AUDIO_PLAY_TYPE_UPCM)
                            read_size=   AUDIO_PLAY_PKT_SIZE/2;

						 else if(usr->a_fmt==AUDIO_PLAY_TYPE_AAC)
						    read_size = AAC_HEAD_SIZE;
							
						 else
                            read_size=AUDIO_PLAY_PKT_SIZE;

                        first_audio_flag=1;
                        ret=1;
						gettimeofday(&start,0);
                    }	
                }
                else
                {///��������״̬	
					
                    ret=read_net_audio_data(accept_fd,(char*)net_rec_buf,read_size); 
					//printf("ret=%d\n",ret);
					if(u1>=10)
					{
						u1=0;
						gettimeofday(&stop,0);
						timeval_subtract(&diff,&start,&stop);
						printf("��ʱ:%d��%d ΢��\n",(int)diff.tv_sec, (int)diff.tv_usec); 
						gettimeofday(&start,0); 

					}
					else
						u1++;
					

		     	 	if(ret>0)
                    {
                        usr->sock_attr.recv_buf_remain=get_fd_in_buffer_num(accept_fd);
                        usr->recv_info.total_recv+=ret;

						if(first_audio_flag)
                   
                        {///<��ǰ��Э��Ҫ����VIEWER_SUBSCRIBE_AUDIO_START����
                            first_audio_flag=0;
                            if(memcmp(proto_head,net_rec_buf,sizeof(proto_head))==0)
                            {
                                printf("receive a VIEWER_SUBSCRIBE_AUDIO_START cmd!\n");
                                gtloginfo("receive a VIEWER_SUBSCRIBE_AUDIO_START cmd!\n");
								if(usr->a_fmt==AUDIO_PLAY_TYPE_UPCM) 
								{
									pbuf=(char*)net_rec_buf+ret;
									ret2=read_net_audio_data(accept_fd,pbuf,proto_start_len);///<����start�����ȵ���Ƶ
									if(ret2<0)
									{
										ret=ret2;
									}
									else
									{
										pbuf=(char*)net_rec_buf+proto_start_len;
									}
								}
								else if(usr->a_fmt==AUDIO_PLAY_TYPE_AAC) //aac �ٶ�36-8�ֽ�,�����������
								{
									pbuf=(char*)net_rec_buf;
									ret2=read_net_audio_data(accept_fd,pbuf+AAC_HEAD_SIZE,proto_start_len-AAC_HEAD_SIZE);///<����start�����ȵ���Ƶ,���ӵ�������
									if(ret2<0)
									{
										ret=ret2;
									}		
									continue;
									
								
								}
                            }
                            else
                            {
								pbuf=(char*)net_rec_buf;            ///<û���յ�VIEWER_SUBSCRIBE_AUDIO_START
                            }
                        }
                        else
						{
							pbuf=(char*)net_rec_buf;
							if(usr->a_fmt==AUDIO_PLAY_TYPE_AAC){
										 
								i_pbuf=(int*)net_rec_buf;
								if(*i_pbuf!=0x77061600){
									printf("AAC read head ERR! %d\n",*i_pbuf);
									gtloginfo("AAC read head ERR! %d\n",*i_pbuf);
									continue;
								}

								ret2=read_net_audio_data(accept_fd,net_rec_buf+AAC_HEAD_SIZE,AAC_HEAD_SIZE);
								if(ret2<0){
									ret=ret2;
									goto HANDLE;
								}
								i_pbuf=(int *)(net_rec_buf+AAC_HEAD_SIZE);
								printf("will read len=%d\n",*i_pbuf);

								if(*i_pbuf>=MAX_AAC_PKT_SIZE||*i_pbuf<=0){
									printf("AAC read length  ERR [%d]!\n",*i_pbuf);
									gtloginfo("AAC read frame length ERR [%d]!\n",*i_pbuf);
									continue;
								}
								ret2=read_net_audio_data(accept_fd,net_rec_buf+AAC_HEAD_SIZE*2,*i_pbuf);
								if(ret2<0){
								  ret=ret2;
								}
							}
							
		

                       }
HANDLE:
					if(ret>0)
					{


#define DEBUG
#ifdef DEBUG
						//write_rawaudio2file_send(pbuf,read_size);
						//write_rawaudio2file_send(pbuf,4096);
					
						//gettimeofday(&start,0);
#endif
#ifdef DEBUG
						//gettimeofday(&stop,0);
						//timeval_subtract(&diff,&start,&stop);
						//printf("�ܼ���ʱ:%d��%d ΢��\n",(int)diff.tv_sec, (int)diff.tv_usec); 
#endif							
						//Ŀǰ����0x51000��һ��buffer��������
						/*
						if(usr->a_fmt==AUDIO_PLAY_TYPE_UPCM) 
						{
							//ret2=write_media_resource(get_audio_dec(0),pbuf,read_size,0);
							memcpy(head_pbuf+4,pbuf,read_size);
							play_audio_buf(NULL,0,0,head_pbuf,read_size,0,0);
							
						}
						*/
						if(usr->a_fmt==AUDIO_PLAY_TYPE_AAC)
						{
							if(	write_media_resource((media_source_t *)get_audio_dec(usr->aenc_no),(void*)net_rec_buf ,ret2+AAC_HEAD_SIZE*2,0)==0)
							
								printf("write_media_resource ret=%d\n",ret2+AAC_HEAD_SIZE);
						}
						usr->recv_info.total_play+=ret2;
                        usr->recv_info.total_recv+=ret2;


					}
                   
                    else if (ret==0)
                    {
                       ret=-140;   ///<���ӱ������ر�
                    }
                    
                }
                if(ret<=0)
                {
                    if(ret==-EAGAIN)
                    {///�������ʱ
                        printf     ("rtnet_aplay_thread �յ�һ��������������\n");
                        gtloginfo("rtnet_aplay_thread �յ�һ��������������\n");
                        continue;
                    }
                    else if(ret==-EINTR)
                    {
                        continue;
                    }
                    else if(ret==-ETIMEDOUT)
                    {
                        printf     ("ETIMEDOUT ����:�ͻ�:%s����Ƶ�������ӳ�ʱ\n",inet_ntoa(usr->addr.sin_addr));
                        gtloginfo("ETIMEDOUT ����:�ͻ�:%s����Ƶ�������ӳ�ʱ\n",inet_ntoa(usr->addr.sin_addr));
                        break;
                    }
                    else
                    {
                        if(ret==-140)
                        {
                            printf     ("��Ƶ��������:%d(%s)���ر�,ret=%d\n",usr->fd,inet_ntoa(usr->addr.sin_addr),ret);
                            gtloginfo("��Ƶ��������:%d(%s)���ر�,ret=%d\n",usr->fd,inet_ntoa(usr->addr.sin_addr),ret);
                        }
                        else
                        {
                            printf     ("��Ƶ��������:%d(%s)�Ͽ�,ret=%d\n",usr->fd,inet_ntoa(usr->addr.sin_addr),ret);
                            gtloginfo("��Ƶ��������:%d(%s)�Ͽ�,ret=%d\n",usr->fd,inet_ntoa(usr->addr.sin_addr),ret);
							
                        }
                        break;
                    }
				

                }
				}
			}
            else
            {
                printf     ("rtnet_aplay_thread FD_ISSET(accept_fd,&read_fds)!=1!!!\n");
                gtloginfo("rtnet_aplay_thread FD_ISSET(accept_fd,&read_fds)!=1!!!\n");
                break;               
            }
			
        }
		//�Ͽ�����

		stop_down_audio2enc(usr->aenc_no);
        pthread_mutex_lock(&svr->s_mutex);
		



        del_aplay_usr_list(usr);   //ɾ���û�
        pthread_mutex_unlock(&svr->s_mutex);
		FD_CLR(accept_fd,&read_fds);
        close(accept_fd);          //�ر���������
        accept_fd=-1;    
    }
    return NULL;
}

/** 
 *   @brief     ��������������Ƶ���з����socket�Լ��̳߳�
 *   @return   0��ʾ�ɹ�,��ֵ��ʾʧ��
 */ 
int create_rtnet_aplay_servers(void)
{
    tcprtimg_svr_t *p=get_rtimg_para();
    aplay_server_t     *svr=&p->aplay_server;
    aplay_usr_t          *usr=NULL;
    int                   fd;
    int                   i;

    init_net_aplay();
    fd = create_tcp_listen_port(INADDR_ANY,p->audio_play_port); ///<������������Ƶ����socket
    if(fd<0)
    {
        printf("can't create socket port:%d:%s exit !\n",p->av_svr_port,strerror(-fd));
        printf("can't create socket port:%d:%s exit !\n",p->av_svr_port,strerror(-fd));
        exit(1);
    }
    svr->listen_fd=fd;
    net_activate_keepalive(svr->listen_fd);                             ///<̽��Է��Ͽ�
    listen(svr->listen_fd,p->max_aplay_usrs);             ///<��������������
    for(i=0;i<(p->max_aplay_usrs+1);i++)
    {
        usr=&svr->usr_list[i];
        gt_create_thread(&usr->thread_id, rtnet_aplay_thread, (void*)usr);
    }
    return 0;
}
