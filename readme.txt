
利用mp4v2库进行aac + h264 视频封装成MP4格式.
##gcc h264_aac_2_mp4.c  -L./lib  -lmp4v2 -I./include -o m
####gcc h264_aac_2_mp4.c  sps_pps_parser.c -L./lib  -lmp4v2 -I./include -o m
gcc h264_aac_2_mp4.c  sps.c -L./lib  -lmp4v2 -I./include -o m