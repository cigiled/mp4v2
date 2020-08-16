#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <mp4v2/mp4v2.h>

#define MAX_FRMAES  1024 *256
#define PER_RD_SZ	1024*1024


enum{
	SPS_FRAME,
	PPS_FRAME,
	I_FRAME,
	P_FRAME,
	B_FRAME,
};

typedef struct
{
	int frame_pos; //能够记录1024 *256帧的位置.
	int frame_sz;  //记录帧的大小.
	int frame_type;
}frame_inf_t;

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
		printf("fm_sz:[%d], fm_pos:[%d], fm_type:[%d]\n", inf[i].frame_sz, inf[i].frame_pos, inf[i].frame_type );
		i++;
	}
	printf("all frame : [%d]\n", nums);
}

int write2Mp4(char *wbuf, int len, MP4FileHandle pHandle, int *type)
{
    MP4TrackId videoId;
	unsigned char naluType;
	
	int width = 640;
	int height = 480;
	int frameRate = 15;
	int timeScale = 90000;
    int addStream = 1;
	char *pNalu = NULL;
	
	if (wbuf[0] != 0 || wbuf[1] != 0 || wbuf[2] != 0 || wbuf[3] != 1)
		return -1;
	
	len -= 4;
	pNalu = wbuf+4;
	naluType = pNalu[0]&0x1F;
	
	switch (naluType)
	{
		case 0x07: // SPS
			*type = SPS_FRAME;
			if (addStream)
			{
				videoId = MP4AddH264VideoTrack
						(pHandle, 
						timeScale,              // 一秒钟多少timescale
						timeScale/frameRate,    // 每个帧有多少个timescale
						width,                  // width
						height,                 // height
						pNalu[1],               // sps[1] AVCProfileIndication
						pNalu[2],               // sps[2] profile_compat
						pNalu[3],               // sps[3] AVCLevelIndication
						3);                     // 4 bytes length before each NAL unit
				if (videoId == MP4_INVALID_TRACK_ID)
				{
					printf("Error:Can't add track.\n");
					return -1;
				}
				
				MP4SetVideoProfileLevel(pHandle, 0x7F);
				addStream = 0;
			}

			MP4AddH264SequenceParameterSet(pHandle, videoId, pNalu, len);
			break;
		
		case 0x08: // PPS
			*type = PPS_FRAME;		
		
			MP4AddH264PictureParameterSet(pHandle, videoId, pNalu, len);
			break;

		default:
			wbuf[0] = (len>>24)&0xFF;
			wbuf[1] = (len>>16)&0xFF;
			wbuf[2] = (len>>8)&0xFF;
			wbuf[3] = (len>>0)&0xFF;

			MP4WriteSample(pHandle, videoId, wbuf, len+4, MP4_INVALID_DURATION, 0, 1);
			break;
	}
	
	return 0;
}

int write_h264_nalu(const char *srcFile, const char *dstFile)
{	
	int sqe = 0;
	int sz = 0;
    int pos = 0;
    int len = 0;
	int in_fz  = 0;
	int frames = 0;
	int remain = 0;
	int file_sz = 0;
	char end_flag = 1;
	char *start = NULL;
	
	char wbuf[1024 * 720] = {0};
	char *rbuf = malloc(PER_RD_SZ);
	
	frame_inf_t  frame[MAX_FRMAES];
	memset(&frame, 0, sizeof(frame));

	struct stat st;
	
	lstat(srcFile, &st);
	in_fz = st.st_size;

	FILE *infd = fopen(srcFile, "rb");
    if(!infd)
        return -1;
	
	//1、create mp4.
	MP4FileHandle outHandle = MP4Create(dstFile, 0);
    if(outHandle == MP4_INVALID_FILE_HANDLE)
    {
		printf("ERROR:Create mp4 handle fialed.\n");
		return -1;
    }
	
	int timeScale = 90000;
    MP4SetTimeScale(outHandle, timeScale);
	
	//2、write h264 frames
	memset(wbuf, 0, sizeof(wbuf));
    while(1)
    {
		if(end_flag)
		{
			pos = 0;
			if((len = fread(rbuf, 1, PER_RD_SZ, infd)) <= 0)
			{
				perror("ERROR:");
				break;
			}
		}
		
		while(1)
		{
			if((rbuf[pos+0] == 0 && rbuf[pos+1] == 0 && rbuf[pos+2] == 0 && rbuf[pos+3] == 1)
				|| (rbuf[pos+0] == 0 && rbuf[pos+1] == 0 && rbuf[pos+2] == 1))
			{				
				(rbuf[pos+2] == 1)?(sqe = 3):(sqe = 4);
				
				if(!start)
				{ //首次检测到帧.
					int pos1 = file_sz + pos;
					frame[frames].frame_pos = pos1; //记录帧的在文件的位置, 便于以后查找.
					
					start = &rbuf[pos];
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
						memcpy(wbuf + remain, &rbuf[0], pos); //加上 上次不完整部分数据.
						sz = pos + remain;
						remain = 0; //上次read 剩下的不完整帧.
					}
					else
					{
						sz = pos2 - frame[frames - 1].frame_pos;
						memcpy(wbuf, start, sz);
					}

					#if  0
					if((rbuf[pos+4] == 0x25) || (rbuf[pos+3] == 0x25))
					{
						print_tnt(wbuf, 6, "end:");
						printf("--[%d]:[%d]:[%d]==[%d]:[%d]->\n", frames+1, sz, remain, pos2, frame[frames - 1].frame_pos);
					}
					#endif
					
					write2Mp4(wbuf, sz, outHandle, &type);
		
					frame[frames].frame_pos    = pos2;
					frame[frames-1].frame_type = type;
					frame[frames-1].frame_sz   = sz;
					
					memset(wbuf, 0, sizeof(sz));
					start = &rbuf[pos];
					frames++;
					pos += sqe;
					end_flag = 0;
				
					continue;
				}
			}
			else if( len == pos)
			{//存下这次read帧中, 剩下的不完整帧数据.
				remain = file_sz + pos - frame[frames-1].frame_pos;
				
				if(remain > 0)
				{
					int add = frame[frames-1].frame_pos - file_sz;
					memset(wbuf, 0, sizeof(sz));
					memcpy(wbuf, &rbuf[add] ,remain);
					if(file_sz + pos == in_fz)
					{ //写最后一帧数据.
						int type = 0;
						write2Mp4(wbuf, remain, outHandle, &type);
	
						frame[frames-1].frame_type = type;
						frame[frames-1].frame_sz   = remain;
					}
				}

				end_flag = 1;
				file_sz += len;
				break;
			}
			
			pos++;
		}
    }

	print_frmae_info(frame, frames);
	
	free(rbuf);
	MP4Close(outHandle, 0);
	fclose(infd);

    return 0;
}

int write_h264(const char *inFile, const char *outFile)
{
	
	if(inFile == NULL)
		printf("INput file is NULL, exit\n");

	const char *outf = outFile ? outFile : "test.mp4";
	
	write_h264_nalu(inFile, outf);
 
    return 0;
}

int main(int argc, char *argv[])
{
   if (write_h264(argv[1], argv[2]) )
   {
		printf("Error:Packet to Mp4 fail.\n");
		return -1;
   }
	
    return 0;
}
