#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <mp4v2/mp4v2.h>

#if 0
#include "sps_pps_parser.h"
#else
#include "sps.h"

#endif

#define MAX_FRMAES  1024 *256
#define PER_RD_SZ	1024*1024

#define uint16_t  unsigned short 
#define uint8_t   unsigned char
#define uint32_t  unsigned int

enum
{
	PK_BY_FILE = 0,
	PK_BY_REAL_STREAM
}PK_type_t;

enum
{
	SPS_FRAME,
	PPS_FRAME,
	I_FRAME,
	P_FRAME,
	B_FRAME,
};

typedef struct 
{
	FILE *			v_fd;
	FILE *			a_fd;
	MP4FileHandle   mp4_hd;
}File_handle_t;

typedef struct
{
	const uint8_t* pSequence;
	uint16_t       sequenceLen;
}SPS_cfg_t;

typedef struct
{
	const uint8_t* pPict;
	uint16_t       pictLen;
}PPS_cfg_t;

typedef struct 
{
	MP4TrackId    trackId;
	int32_t       frameRate; //为了方便动态修改,而不只是从nalu中获取.
	uint32_t      timeScale;
	MP4Duration   sampleDuration;
	uint16_t      width;
	uint16_t      height;
	uint8_t       AVCProfileIndication;
	uint8_t       profile_compat;
	uint8_t       AVCLevelIndication;
	uint8_t       sampleLenFieldSizeMinusOne;
	
	int32_t 	  fsz; //PK_BY_FILE模式下为vidoe文件的大小，PK_BY_FILE模式则为-1.
	SPS_cfg_t     *sps;
	PPS_cfg_t     *pps;
}Video_tk_cfg_t;

typedef struct 
{
	uint32_t timeScale;
	MP4Duration sampleDuration;
	uint8_t audioType;

	MP4TrackId     trackId;
	const uint8_t* pConfig;
	uint32_t       configSize;
}Audio_tk_cfg_t;

typedef struct 
{
	File_handle_t  hd;
	Video_tk_cfg_t *v;
	Audio_tk_cfg_t *a;
}tk_para_cfg_t;

tk_para_cfg_t *tk_m = NULL;

typedef struct
{
	int frame_pos; //能够记录1024 *256帧的位置.
	int frame_sz;  //记录帧的大小.
	int frame_type;
}frame_inf_t;

enum
{
	MV_MPEG_4,
	MV_MPEG_2,
}Mpeg_version;

enum
{
	AP_AAC_MAIN = 0,
	AP_AAC_LC,
	AP_AAC_SSR
}Aac_profile;

enum
{
	SP_96000 = 0, 
	SP_88200,
	SP_64000,
	SP_48000,
	SP_44100,
	SP_32000,
	SP_24000,
	SP_22050,
	SP_16000,
	SP_12000,
	SP_11025,
	SP_8000,
	SP_7350, //12: 
	SP_MAX,
}Aac_sampling;

enum
{
	CH_AOT_0 = 0,          //Defined in AOT Specifc Config
	CH_FC_1,  	           //1 channel: front-center
	CH_FLR_2,              //2 channels: front-left, front-right
	CH_FCLR_3,             //3 channels: front-center, front-left, front-right
	CH_FCLR_BC_4,          //4 channels: front-center, front-left, front-right, back-center
	CH_FCLR_BLR_5,         //5 channels: front-center, front-left, front-right, back-left, back-right
	CH_FCLR_BLR_LFE_6,     //6 channels: front-center, front-left, front-right, back-left, back-right, LFE-channel
	CH_FCLR_SLR_BLR_LFE_7, //8 channels: front-center, front-left, front-right, side-left, side-right, back-left, back-right, LFE-channel
	CH_MAX,
}Aac_chn;

typedef struct
{
	char mpeg_ver;
	char profile;
	char sampling;
	char chns;
}Aac_t;

void print_tnt(char *buf, int len, const char *comment)
{
	int i = 0;
	printf("\r\n%s start buf:%p len:%d \r\n", comment, buf, len);
	for(i = 0; i < len; i++)
	{
    	 printf("%02x ", buf[i] & 0xff); //&0xff 是为了防止64位平台中打印0xffffffa0 这样的，实际打印0xa0即可
	}
	printf("\r\n");
}

int print_frmae_info(frame_inf_t * inf, int nums)
{
	int i = 0;

	while( i < nums)
	{
		printf("seq:[%d], fm_sz:[%d], fm_pos:[%d], fm_type:[%d]\n", i+1, inf[i].frame_sz, inf[i].frame_pos, inf[i].frame_type );
		i++;
	}
	printf("all frame : [%d]\n", nums);
}

int add_video_tk(MP4FileHandle hdl, Video_tk_cfg_t *v)
{
	printf("[%d %d %d]:[%d:%ld]\n", v->frameRate, v->width, v->height, v->timeScale, v->sampleDuration );
	MP4TrackId video_tk = 0;
	video_tk = MP4AddH264VideoTrack(
					hdl,
					v->timeScale,
					v->timeScale / v->frameRate,//每一帧的字节数,[TWT]:出现这里算错，导致无法解码.
					v->width,
					v->height,
					v->AVCProfileIndication,
					v->profile_compat,
					v->AVCLevelIndication,
					3);

	MP4SetVideoProfileLevel(hdl, 0x7f); //  Simple Profile @ Level 3  
	v->trackId = video_tk;

	return 0;
}

int add_audio_tk(MP4FileHandle hdl, Audio_tk_cfg_t *a)
{
	MP4TrackId audio_tk = 0;
	audio_tk = MP4AddAudioTrack(hdl, a->timeScale, a->sampleDuration, a->audioType);
	MP4SetAudioProfileLevel(hdl, 0x2);  
	a->trackId = audio_tk;
	
	//[TVT]:pConfig 要么设定死，要么解析aac数据.
	uint8_t buf3[2] = { 0x11, 0x88 };
	MP4SetTrackESConfiguration(hdl, a->trackId, buf3, 2);
	
	return 0;
}

int spare_sps(char *date, int sz, Video_tk_cfg_t *v)
{
	#if 0
	SPS _sps; 
	float fps = 0.0;  
	get_bit_context buffer; 
	
	memset(&buffer, 0, sizeof(get_bit_context));  
	buffer.buf = date;  
	buffer.buf_size = sz - 1;  
	
	//从sps pps中获取信息  
	h264dec_seq_parameter_set(&buffer, &_sps); 

	v->width  = h264_get_width(&_sps);  
	v->height = h264_get_height(&_sps); 
	int ret = h264_get_framerate(&fps, &_sps);  
	if (ret == 0) 
		v->frameRate = (double)fps;  
	#else
	sps_t _sps; 
	char tmpbuf[40] ={'\0'};
	int len = sz;
	int rc = nal_to_rbsp(0, date, &sz, tmpbuf, &len );
	bs_t* b = bs_new(tmpbuf, len);

	//从sps pps中获取信息  
	read_seq_parameter_set_rbsp(b, &_sps); 

	v->width  = h264_get_width(&_sps);  
	v->height = h264_get_height(&_sps); 
	bs_free(b);
	#endif
	
	v->AVCProfileIndication = date[1];
	v->profile_compat = date[2];
	v->AVCLevelIndication = date[3];
	
	return 0;
}

int spare_aac(FILE *fd,  Aac_t * aac)
{
	if(!aac)
	{
		printf("Input aac point is NULL, exit\n");
		return -1;
	}
	
	if(fd <= 0)
	{
		perror("Open file failed :");
		return -1;
	}
	
	char head[9] = {'\0'};
	int sz = fread(head, sizeof(head), 1, fd);
	if(sz > 0)
	{
		if((head[0]&0xff == 0xff) && (head[1]&0xf0 == 0xf0))
		{
			aac->mpeg_ver = (head[1] & 0x08) >> 3;
			aac->profile  = (head[2] & 0xc0) >> 6;
			aac->sampling = (head[2] & 0x3c) >> 2;
			aac->chns 	  = (head[2] & 0x01) << 2 | ((head[3] & 0xc0) >> 6);
		}
	}

	fseek(fd, 0L, SEEK_SET);
}

int get_sampling(char samp_seq)
{
	int samp[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000};
	if( (samp_seq > sizeof(samp))  || (samp_seq < 0))
	{
		printf("Input sampling que is error, do not rang.\n");
		return -1;
	}
	
	return samp[samp_seq];
}

int pri_aac_inf(Aac_t *aac)
{
	if(!aac)
		return -1;
	
	switch(aac->mpeg_ver)
	{ 
		case 0: printf("mpeg version :""MPEG_4\n");break;
		case 1: printf("mpeg version :""MPEG_2\n");break;
		default:printf("mpeg version :""unknown\n");break;
	}
	
	switch(aac->profile)
	{ 
		case 0: printf("profile      :""Main\n");break;
		case 1: printf("profile      :""LC\n");break;
		case 2: printf("profile      :""SSR\n");break;
		default:printf("profile      :""unknown\n");break;
	}

	int samp = 0;
	switch(aac->sampling)
	{
		case 0:  samp = 96000;break;
		case 1:  samp = 88200;break;
		case 2:  samp = 64000;break;
		case 3:  samp = 48000;break;
		case 4:  samp = 44100;break;
		case 5:  samp = 32000;break;
		case 6:  samp = 24000;break;
		case 7:  samp = 22050;break;
		case 8:  samp = 16000;break;
		case 9:  samp = 12000;break;
		case 10: samp = 11025;break;
		case 11: samp = 8000; break;
		default:printf("sampling     :""unknown\n");break;
	}

	printf("sampling     :""%dHz\n", samp);
	
	switch(aac->chns)
	{ 
		case 0: printf("chns         :""0\n");break;
		case 1: printf("chns         :""1\n");break;
		case 2: printf("chns         :""2\n");break;
		default:printf("chns   :""unknown\n");break;
	}
	
	return 0;
}

int write_video_2_mp4(char *wbuf, int len, tk_para_cfg_t *tk, int *type)
{
	unsigned char naluType;

	MP4FileHandle pHandle = tk->hd.mp4_hd;
	Video_tk_cfg_t *v_m = tk->v;
	
    int addStream = 1;
	char *pNalu = NULL;
	
	if (wbuf[0] != 0 || wbuf[1] != 0 || wbuf[2] != 0 || wbuf[3] != 1)
		return -1;
	
	len -= 4;
	pNalu = wbuf+4;
	naluType = pNalu[0]&0x1F;
	
	switch (naluType)
	{
		case 0x07:
		case 0x08: 
			if(naluType == 0x07)
			{// SPS
				*type = SPS_FRAME;									
				spare_sps(pNalu +1, len -1, v_m);
				
				add_video_tk(pHandle, v_m);
				MP4AddH264SequenceParameterSet(pHandle, v_m->trackId, pNalu, len);
			}
			else
			{ //PPS
				*type = PPS_FRAME;		
				MP4AddH264PictureParameterSet(pHandle, v_m->trackId, pNalu, len);
			}

			break;
			
		default:
			wbuf[0] = (len>>24)&0xFF;
			wbuf[1] = (len>>16)&0xFF;
			wbuf[2] = (len>>8)&0xFF;
			wbuf[3] = (len>>0)&0xFF;
			*type = I_FRAME;
			
			MP4WriteSample(pHandle, v_m->trackId, wbuf, len+4, MP4_INVALID_DURATION, 0, 1);
			break;
	}
	
	return 0;
}

int read_aac_frame(char *rbuf, int len, FILE * a_fd)
{
		char aac_head[7] = {0};
		
		int ret  = fread(aac_head, 1, sizeof(aac_head),  a_fd);
		if(ret <= 0 )
		{
			printf("read aac head failed,  exit !\n");
			return -1;
		}

		//printf("[%02x %02x %02x]\n", aac_head[0]&0xff, aac_head[1]&0xff, aac_head[2]&0xff);
		int body_size = (aac_head[3] & 0x03) << 11;
		body_size += aac_head[4] << 3;
		body_size += (aac_head[5] & 0xe0) >> 5;
		
		ret = fread(rbuf, 1, body_size - 7,  a_fd);
		if(ret <= 0 )
		{
			printf("read aac frame body failed,  exit !\n");
			return -1;
		}
		
	return ret;
}

int write_audio_2Mp4(char *wbuf, int len, tk_para_cfg_t *tk)
{
		//print_tnt(wbuf, 12, "audio");
		
		if(MP4WriteSample(tk->hd.mp4_hd, tk->a->trackId, wbuf, len, MP4_INVALID_DURATION, 0, 1) == false)
		{
			
		}
	return 0;
}

int parse_pps_sps(Video_tk_cfg_t *v	)
{
	if(!v)
		return  -1;
	
	SPS_cfg_t  *sps;
	PPS_cfg_t  *pps;

	return 0;
}

uint32_t get_time_ms()
{
	struct timeval _stime;
	
	gettimeofday(&_stime, NULL);
	return (_stime.tv_sec * 1000 + _stime.tv_usec / 1000);
}

int write_h264_aac(tk_para_cfg_t *tk)
{	
	//video --- 
	int sqe = 0;
	int sz = 0;
    int pos = 0;
    int len = 0;
	int frames = 0;
	int remain = 0;
	int file_sz = 0;
	char end_flag = 1;
	char *start = NULL;
	char wbuf[1024 * 720] = {0};
	char *v_buffer = malloc(PER_RD_SZ);
	frame_inf_t  v_frame[MAX_FRMAES];
	memset(&v_frame, 0, sizeof(v_frame));
	memset(wbuf, 0, sizeof(wbuf));

	//audio --- 
	int i = 0;
	int re_sz  = 0;
	int offse = 0;
	int a_remain = 0;
	int frame_len = 0;
	int last_frame_len = 0;
	
	int FM_SE = 1024*16;
	int RD_SE  = 1024*1024;
	
	char aacframe[1024 * 16] = {'\0'};
	char *a_buffer=( char *)malloc(RD_SE);

	//--------------------------------------
	uint32_t tick_exp_tmp  = 0;
    uint32_t tick_exp = 0; 

	static int szs  = 0;
	char audio_end = 0;
	char video_end = 0;

	//计算音频 和 视频 每帧的时间.
	uint32_t audio_tick_gap = tk->a->sampleDuration * 1000 / tk->a->timeScale;
	uint32_t video_tick_gap = (1000.0 + 1.0) / tk->v->frameRate ;
	printf("[%ld]:[%d]\n", tk->a->sampleDuration, tk->a->timeScale);
	printf("++++[%d]:[%d]++++\n", audio_tick_gap, video_tick_gap);
	uint32_t audio_tick_now, video_tick_now, last_update; 
	
    //add   trick if have.
    //if(tk->a->pConfig)
    {
    	//parse ...
		add_audio_tk(tk->hd.mp4_hd, tk->a);
    }
	
	if(tk->v->sps && tk->v->pps)
	{
		parse_pps_sps(tk->v);
		Video_tk_cfg_t *v = tk->v;
	
		add_video_tk(tk->hd.mp4_hd, tk->v);
		MP4AddH264SequenceParameterSet(tk->hd.mp4_hd, v->trackId, v->sps->pSequence, v->sps->sequenceLen);
		MP4AddH264PictureParameterSet(tk->hd.mp4_hd, v->trackId, v->pps->pPict, v->pps->pictLen);
	}

	video_tick_now = audio_tick_now = get_time_ms();
    while(1)
    {
    	last_update = get_time_ms();
    	//write video 
		if( (tk->hd.v_fd > 0) && (!video_end))
		{
			if(last_update - video_tick_now > video_tick_gap - tick_exp)
			{
					if(end_flag)
					{
						pos = 0;
						if((len = fread(v_buffer, 1, PER_RD_SZ, tk->hd.v_fd)) <= 0)
						{
							video_end = 1;
							continue;
						}
					}

					while(1)
					{
						if((v_buffer[pos+0] == 0 && v_buffer[pos+1] == 0 && v_buffer[pos+2] == 0 && v_buffer[pos+3] == 1)
							|| (v_buffer[pos+0] == 0 && v_buffer[pos+1] == 0 && v_buffer[pos+2] == 1))
						{				
							(v_buffer[pos+2] == 1)?(sqe = 3):(sqe = 4);
							
							if(!start)
							{ //首次检测到帧.
								int pos1 = file_sz + pos;
								v_frame[frames].frame_pos = pos1; //记录帧的在文件的位置, 便于以后查找.
								
								start = &v_buffer[pos];
								pos += sqe;
								frames++;
								end_flag = 0;
								continue;
							}
							else
							{
								int type = 0;
								int pos2 = file_sz + pos; //加上上次read的长度，得到当前在文件中的位置.
								
								if(remain > 0)
								{
									memcpy(wbuf + remain, &v_buffer[0], pos); //加上 上次不完整部分数据.
									sz = pos + remain;
									remain = 0; //上次read 剩下的不完整帧.
								}
								else
								{
									sz = pos2 - v_frame[frames - 1].frame_pos;
									memcpy(wbuf, start, sz);
								}
	
								write_video_2_mp4(wbuf, sz, tk, &type);

								v_frame[frames].frame_pos    = pos2;
								v_frame[frames-1].frame_type = type;
								v_frame[frames-1].frame_sz   = sz;
								
								memset(wbuf, 0, sizeof(sz));
								start = &v_buffer[pos];
								frames++;
								pos += sqe;
								end_flag = 0;
							
								continue;
							}
						}
						else if( len == pos)
						{//存下这次read帧中, 剩下的不完整帧数据.
							remain = file_sz + pos - v_frame[frames-1].frame_pos;	
							if(remain > 0)
							{
								int add = v_frame[frames-1].frame_pos - file_sz;
								memset(wbuf, 0, sizeof(sz));
								memcpy(wbuf, &v_buffer[add] ,remain);
								if(file_sz + pos == tk->v->fsz)
								{ //写最后一帧数据.
									int type = 0;
									write_video_2_mp4(wbuf, remain, tk, &type);
				
									v_frame[frames-1].frame_type = type;
									v_frame[frames-1].frame_sz   = remain;
								}
							}

							end_flag = 1;
							file_sz += len;
							break;
						}
						
						pos++;
					}

					video_tick_now = get_time_ms();
			 }
		}

		//write audio. 
		if((tk->hd.a_fd > 0) && (!audio_end))
		{
			if(last_update - audio_tick_now > audio_tick_gap - tick_exp)
			{	
			   #if 0
				int lenth = read_aac_frame(audio_buff, aac_sz, tk->hd.a_fd);
				if(lenth <= 0)
				{
					audio_end = 1;
					continue;
				}
			
				szs += lenth;
				write_audio_2Mp4(audio_buff, lenth, tk);
				memset(audio_buff, 0,  aac_sz);
				audio_tick_now = get_time_ms();
				#else
				memset(a_buffer, '\0', RD_SE);
				re_sz = fread(a_buffer, 1, RD_SE, tk->hd.a_fd);
				if( re_sz<= 0)
				{
					printf("read over !\n");
					audio_end = 1;
					continue;
				}
				
				while(1)
				{
					//Sync words: 0xfff;
					if((a_buffer[i]&0xff == 0xff) && (a_buffer[i+1]&0xf0 == 0xf0) )
					{   
						//frame_length: 13 bit
						frame_len = 0;
						frame_len |= (a_buffer[i+3] & 0x03) <<11;   //low    2 bit
						frame_len |= (a_buffer[i+4] & 0xff) <<3;	//middle 8 bit
						frame_len |= (a_buffer[i+5] & 0xe0) >>5;	//hight  3bit
						
						memset(aacframe, '\0', FM_SE);
						if(re_sz >= frame_len)
						{
							memcpy(aacframe, &a_buffer[i],frame_len);
							write_audio_2Mp4(&aacframe[7], frame_len -1, tk);
							
							i += frame_len;    //加一帧的长度.
							re_sz -= frame_len;//剩下的帧数据...
							frame_len = 0;
							if(re_sz == 0)
								break;
						}
						else
						{
							a_remain = frame_len - re_sz;
							memcpy(aacframe, &a_buffer[i], re_sz); //保存剩下的帧数.
							
							last_frame_len = frame_len;
							offse = re_sz;
							re_sz = 0;
							frame_len = 0;
							i = 0;
							break;
						}
						continue;
					}
					else
					{
						if(a_remain > 0)
						{
							memcpy(aacframe + offse, &a_buffer[i], a_remain); //保存剩下的帧数.
							write_audio_2Mp4(&aacframe[7], last_frame_len -1, tk);
							
							i += a_remain;    //加上次剩下的帧数据的长度.
							re_sz -= a_remain;//接着上次剩下的帧数据...
							a_remain = 0;
							continue;
						}
					}
					i++;
				}   
				audio_tick_now = get_time_ms();
				#endif
			}
		}

		tick_exp_tmp = get_time_ms();
		tick_exp = tick_exp_tmp - last_update;//计算循环一次的时间间隔.

		if(audio_end && video_end)
			break;

		usleep(800);
    }

	printf("pack end-\n");
	//print_frmae_info(frame, frames);
	
	free(v_buffer);
	free(a_buffer);
    return 0;
}

int set_tk_para(tk_para_cfg_t *tk)
{
	if(!tk)
	{
		printf("Input tk_para_cfg_t obj is null, exit\n");
		exit(-1);
	}

	tk_m = tk;
	
	return 0;
}

int file_init(const char (*files)[36], tk_para_cfg_t *tk)
{
	const char *video_file = files[0];
	const char *audio_file = files[1];
	const char *mp4_file   = files[2];
	Aac_t aac;
	memset(&aac, 0,  sizeof(Aac_t));
	
	printf("****[%s]:[%s]:[%s]\n", files[0], files[1], files[2]);
	//video file
	if(!(tk->hd.v_fd = fopen(video_file, "rb")) )
	{
		perror("Open vidoe file faile:");
		return -1;
	}
	
	struct stat st;
	lstat(video_file, &st);
	tk->v->fsz = st.st_size;
	tk->v->frameRate = 21;
	tk->v->timeScale = 100000;//?????
	tk->v->sps = NULL;
	tk->v->pps = NULL;
	
	//audio file
	if(!(tk->hd.a_fd = fopen(audio_file, "rb")) )
	{
		perror("Open audio file faile:");
		return -1;
	}
	
	spare_aac(tk->hd.a_fd, &aac);
	tk->a->sampleDuration = 1024; //(aac.chns==2) ? (1024*2) : (1024); //AAC的一帧是 1024个采样点数
	tk->a->timeScale = get_sampling(aac.sampling);  //采样率, eg:48000;
	tk->a->audioType = MP4_MPEG4_AUDIO_TYPE;
	//在采样率为44100Hz时（即每秒44100个sample）,	
	//当前AAC一帧的播放时间[frame_duration]是 = 1024* （1000000/44100） = 22.32ms (单位为ms)

	//mp4 file
	tk->hd.mp4_hd = MP4Create(mp4_file, 0);
	if(tk->hd.mp4_hd == MP4_INVALID_FILE_HANDLE)
	{
		printf("Create mp4 handle fialed.");
		return -1;
	}

	MP4SetTimeScale(tk->hd.mp4_hd, tk->v->timeScale);

    pri_aac_inf(&aac);
	return 0;
}

int file_destroy(File_handle_t *hand)
{
	fclose(hand->v_fd);
	fclose(hand->a_fd);
	MP4Close(hand->mp4_hd, 0);
	
	return 0;
}

int write_av_date(tk_para_cfg_t *tk)
{
	write_h264_aac(tk);

	return 0;
}

int pack_av_by_file_stream(const char (*file)[36], tk_para_cfg_t *tk)
{
	file_init(file, tk);
	write_av_date(tk);
	file_destroy(&tk->hd);

	return 0;
}

int pack_av_by_real_stream(const char (*file)[36])
{
    return 0;
}

int pack_av(const char (*file)[36], int type)
{
	if(type == PK_BY_FILE)
	{
		tk_para_cfg_t tk;
		memset(&tk, 0,  sizeof(tk));

		//video
		tk.v = (Video_tk_cfg_t *)malloc(sizeof(Video_tk_cfg_t));
		memset(tk.v, 0, sizeof(Video_tk_cfg_t));

		//audio
		tk.a = (Audio_tk_cfg_t *)malloc(sizeof(Audio_tk_cfg_t));
		memset(tk.a, 0, sizeof(Audio_tk_cfg_t));

		pack_av_by_file_stream(file, &tk);

		free(tk.v);
		free(tk.a);
	}
	else
	{
		pack_av_by_real_stream(file);
	}

	return 0;
}

int parse_argv(int argc, void *argv[], char ( *out_files)[36])
{
	if((argv[1] == NULL) || (argv[2] == NULL) )
	{
		printf("Input file is NULL, exit\n");
		return -1;
	}

	int i = 1;
	char *tmp = NULL;
	while(i < argc)
	{
		if((tmp = strrchr(argv[i], '.')))
		{
			tmp++;
			if( !strcasecmp(tmp, "h264") || !strcasecmp(tmp, "h265"))
				memcpy(out_files[0], (char *)argv[i],  strlen(argv[i]));
			else if( !strcasecmp(tmp, "aac") )
				memcpy(out_files[1], argv[i],  strlen(argv[i]));
		}
		i++;
	}
	
	char *outf = argv[3] ? argv[3] : "test.mp4";
	memcpy(out_files[2], outf,  strlen(outf));
	return 0;
}

int main(int argc, void *argv[])
{
	char files[3][36] = {'\0'};
	
	parse_argv(argc, argv, files);
	
	pack_av(files, PK_BY_FILE);
	
	return 0;
}
