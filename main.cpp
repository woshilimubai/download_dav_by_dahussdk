#include "dhnetsdk.h"
#include <QCoreApplication>
#include "stdio.h"
#include <iostream>
#include <unistd.h>
#include <hiredis/hiredis.h>
#define REDIS "127.0.0.1"
#define DAV_LENGTH 5
using namespace std;

LLONG lDownloadHandle = 0;
FILE *fPic;
BOOL over = 0;//用于标志 视频流是否传输结束，当over=0时，表示为传输结束；当over=1时，表示传输结束。
DWORD downLoadSize = 0;//用于统计 当前下载到的视频流的size
char file_name[60];
char log_txt[50];

int write_log(char *err);
struct tm* get_time(void);
int get_trigger(char *ip);
void CALL_METHOD Disconnect(LLONG lLoginID, char *pchDVRIP, LONG nDVRPort, LDWORD dwUser);
void CALL_METHOD fHaveReConnectCB(LLONG lLoginID, char *pchDVRIP, LONG nDVRPort, LDWORD dwUser);
void CALLBACK TimeDownLoadPosCallBack(LLONG lPlayHandle, DWORD dwTotalSize, DWORD dwDownLoadSize, int index, NET_RECORDFILE_INFO recordfileinfo, LDWORD dwUser);
int CALLBACK DataCallBack(LLONG lRealHandle, DWORD dwDataType, BYTE *pBuffer, DWORD dwBufSize, LDWORD dwUser);

//获取某月份的天数
unsigned char get_days(unsigned int year, char month)
{
    if(month == 1 || month == 3 || month == 5 || month == 7 || month == 8 || month == 10 || month == 12)
        return 31;
    else if(month == 2)
    {
        if((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))
            return 29;
        else
            return 28;
    }
    else
        return 30;
}
/**************download_by_time函数**************/
//功能：按时间下载IPC或NVR中的dav视频
//参数：ip_addr:设备IP地址；port:端口号；user:用户名；pwd：密码；channel_id:通道号；struct tm*:时间点；
//Note：通道号从0开始，IPC的通道号只能为0， NVR的通道号为0、1 、2、3……
//Note: stream_type: 0-主辅码流,1-主码流,2-辅码流
//Note: struct tm; 1900 + tm->tm_year、1 + tm_mon、tm_mday、tm_hour、tm_min、tm_sec
//NOTE:char part1_len, part2_len;// 时间点前一部分、后一部分的视频时长, NOTE：应为正整数
//返回值：下载成功 --> 1
int download_by_time(char* ip_addr, int port, char* user, char* pwd, unsigned char channel_id, struct tm* time)
{
    unsigned int year;
    char month, day, hour, min, sec;
    int stream_type = 1; // 0-主辅码流,1-主码流,2-辅码流
    LLONG login_handle = 0;
    DWORD temp = 0;
    char part1_len = 1; // 时间点前一部分的视频时长, NOTE：应为正整数
    char part2_len = 1; //时间点后一部分的视频时长，NOTE：应为正整数
    NET_DEVICEINFO_Ex info_ex = { 0 };
    int err = 0;
    int n_waittime = 5000;  //超时时间设置为5s
    int n_trytime = 5; //若出现超时，尝试登陆5次
    CLIENT_Init(Disconnect, NULL);
    CLIENT_SetAutoReconnect(fHaveReConnectCB, NULL);
    // 设置连接超时时间和尝试次数
    CLIENT_SetConnectTime(n_waittime, n_trytime);
    login_handle = CLIENT_LoginEx2(ip_addr, port, user, pwd, (EM_LOGIN_SPAC_CAP_TYPE)0, NULL, &info_ex, &err);
    if (login_handle == 0)
    {
        printf("%s 登录失败!\r\n", ip_addr);
        //写日志
        sprintf(log_txt, "'%s'登录失败", ip_addr);
        write_log(log_txt);
    }
    else
    {
        printf("%s 登陆成功!\r\n", ip_addr);
        //****************开启录像下载***********************
        // 设置查询时的录像码流类型，此处设置码流类型为主码流
        CLIENT_SetDeviceMode(login_handle, DH_RECORD_STREAM_TYPE, &stream_type);
        NET_RECORDFILE_INFO info = { 0 };
        LPNET_TIME time_start = (LPNET_TIME)malloc(sizeof(LPNET_TIME));
        LPNET_TIME time_end = (LPNET_TIME)malloc(sizeof(LPNET_TIME));

        //先对下载的起始和截止时间进行赋初值
        time_start->dwYear = time->tm_year + 1900;
        time_start->dwMonth = time->tm_mon + 1;
        time_start->dwDay = time->tm_mday;
        time_start->dwHour = time->tm_hour;
        time_start->dwMinute = time->tm_min - part1_len;
        //time_start->dwMinute = time->tm_min - 50;//test
        time_start->dwSecond = time->tm_sec;
        time_end->dwYear = time->tm_year  + 1900;
        time_end->dwMonth = time->tm_mon + 1;
        time_end->dwDay = time->tm_mday;
        time_end->dwHour = time->tm_hour;
        time_end->dwMinute = time->tm_min + part2_len;
        time_end->dwSecond = time->tm_sec;

        //对下载的起始和截止时间进行重新计算
        year = time->tm_year + 1900;//年
        month = time->tm_mon + 1;//月
        day = time->tm_mday;//日
        hour = time->tm_hour;//时
        min = time->tm_min;//分
        sec = time->tm_sec;//秒
        //起始时间
        if(min < part1_len)//分
        {
            time_start->dwMinute = min + 60 - part1_len;
            if(hour < 1)//时
            {
                time_start->dwHour = 23;
                if(day < 2)//天
                {
                    time_start->dwDay = get_days(year, month - 1);//上个月的总天数，即上个月的最后一天
                    if(month < 2)//月
                    {
                        time_start->dwMonth = 12;
                        time_start->dwYear = year - 1;//年
                        time_start->dwDay = get_days(time_start->dwYear, time_start->dwMonth);
                    }
                    else
                    {
                        time_start->dwMonth = month - 1;
                    }
                }
                else
                {
                    time_start->dwDay = day - 1;
                }
            }
            else
            {
                time_start->dwHour = hour - 1;
            }
        }
        //截止时间
        if(min + part2_len > 59)//分
        {
            time_end->dwMinute = min + part2_len - 60;
            if(hour + 1 > 23)//时
            {
                time_end->dwHour = 0;
                if(day + 1 > get_days(year, month))//天
                {
                    time_end->dwDay = 1;//即1日
                    if(month + 1 > 12)//月
                    {
                        time_end->dwMonth = 1;//即1月
                        time_end->dwYear = time_end->dwYear + 1;//年
                    }
                    else
                    {
                        time_end->dwMonth = month + 1;
                    }
                }
                else
                {
                    time_end->dwDay = day + 1;
                }
            }
            else
            {
                time_end->dwHour = hour + 1;
            }
        }
        sprintf(file_name, "%s-%d-%d年%d月%d日-%d_%d_%d-%d_%d_%d.dav", ip_addr, channel_id, time_start->dwYear, time_start->dwMonth, time_start->dwDay, time_start->dwHour,
                time_start->dwMinute, time_start->dwSecond, time_end->dwHour, time_end->dwMinute, time_end->dwSecond);

        try
        {
            // 函数形参 sSavedFileName 和 fDownLoadDataCallBack 需至少有一个为有效值，否则入参有误
            lDownloadHandle = CLIENT_DownloadByTimeEx(login_handle, channel_id, EM_RECORD_TYPE_ALL, time_start, time_end, file_name, TimeDownLoadPosCallBack, NULL, DataCallBack, NULL);
            if (lDownloadHandle == 0)
            {
                printf("CLIENT_DownloadByTimeEx: failed! Error code: %x.\n", CLIENT_GetLastError());
                //写日志
                sprintf(log_txt, "'%s'CLIENT_DownloadByTimeEx: failed! Error code: %x", ip_addr, CLIENT_GetLastError());
                write_log(log_txt);
            }
            else
            {
                //打开要下载的文件
                fPic = fopen(file_name, "wb+");
                while (!over)
                {
                    sleep(3);//这里的延时并不会影响回调函数的正常执行
                    temp = downLoadSize;
                    sleep(3);
                    if (downLoadSize == temp)
                    {
                        break;//跳出这个while循环
                    }
                }
                fclose(fPic);
                //system("ffmpeg -i test.dav -c copy -map 0 test.avi -y");
                //system("ffmpeg -i test.dav test.avi");
                CLIENT_StopDownload(lDownloadHandle);
                // Time = (double)cvGetTickCount() - Time;
                // printf("run time = %gms\n", Time / (cvGetTickFrequency() * 1000));
            }
        }
        catch (double d)
        {
            printf("CLIENT_DownloadByTimeEx: failed! Error code: %x.\n", CLIENT_GetLastError());
            //写日志
            sprintf(log_txt, "'%s'CLIENT_DownloadByTimeEx: failed! Error code: %x", ip_addr, CLIENT_GetLastError());
            write_log(log_txt);
        }
    }
    // 释放网络库
    CLIENT_Logout(login_handle);
    CLIENT_Cleanup();
}
/**************get_time函数******************/
// 功能：获取系统当前时间
// 参数：无
// 返回值：struct tm; tm->tm_year（tm_year + 1900 为当前年份）、tm_mon（tm_mon + 1 为当前月份）、tm_mday、tm_hour、tm_min、tm_sec
struct tm* get_time(void)
{
    //获取当前系统时间
    time_t timep;
    struct tm *p_time;
    time (&timep);
    p_time = localtime(&timep);
    return p_time;
}


/**************write_log函数******************/
// 功能：将程序中出现的异常信息写到log文件中
// 参数：异常信息，类型：字符串
// 返回值：写入成功 --> 1; 写入失败 --> 0。
int write_log(char *err)
{
    //获取当前系统时间
    time_t timep;
    struct tm *p_time;
    time (&timep);
    p_time=localtime(&timep);
    printf("%d年%d月%d日 %d:%d:%d\t错误信息：%s\n", 1900 + p_time->tm_year, 1+p_time->tm_mon, p_time->tm_mday, p_time->tm_hour, p_time->tm_min, p_time->tm_sec, err);
    sprintf(log_txt, "%d年%d月%d日 %d:%d:%d\t错误信息：%s\n", 1900 + p_time->tm_year, 1+p_time->tm_mon, p_time->tm_mday, p_time->tm_hour, p_time->tm_min, p_time->tm_sec, err);
    //printf("%s\n", log_txt);
    //将异常信息写到log文件中
    FILE *fp = fopen("error.log", "a");
    if(fp == NULL) //未存在error.log文件
    {
        fp = fopen("error.log", "w+");
    }
    fwrite(log_txt, strlen(log_txt), 1, fp);
    fclose(fp);
    return 1;
}

/**************get_trigger函数******************/
// 功能：获取redis服务器中key:trigger_flag对应的状态
// 参数：redis服务器的IP地址，类型：字符串（例如, "192.168.80.2"）
// 返回值：磁钢触发成功 --> 1; 磁钢未触发 --> 0。
int get_trigger(char *ip)
{
    unsigned char i = 0;
    redisContext *connect;
    redisReply *reply;
    struct timeval timeout = { 1, 500000 }; // { 1, 500000 } --> 1.5 seconds 设置连接等待时间
    while(i <= 2)//尝试三次
    {
        connect = redisConnectWithTimeout(ip, 6379, timeout);
        if (!connect->err) //连接redis服务器成功
        {
            // 获取 trigger状态
            reply = (redisReply *)redisCommand(connect, "GET trigger_flag");
            if(reply->str[0] == '0' || reply->str[0] == '1') //获取的trigger_flag符合规范
            {
                printf("GET trigger_flag: %s\n", reply->str);
                if(reply->str[0] == '1')
                    break;
                freeReplyObject(reply);
                i++;
                sleep(3);

            }
            else //获取的trigger_flag不符合规范
            {
                freeReplyObject(reply);
                i++;
                //写日志
                write_log("获取到的 trigger_flag 不符合规范");
                sleep(3);
            }
        }
        //连接redis服务器失败
       else
        {
           printf("Connection error: %s\n", connect->errstr);
           i++;
           //写日志
           write_log("连接 redis服务器 失败");
           sleep(3);
       }
    }
    if(i == 3 || reply->str == 0x0) //三次尝试获取trigger_flag均失败，或者 trigger_flag 的状态值为“nil”
        return 0;
    else
        return 1;
}

//断线回调
void CALL_METHOD Disconnect(LLONG lLoginID, char *pchDVRIP, LONG nDVRPort, LDWORD dwUser)
{
    cout << "Receive disconnect message, where ip:" << pchDVRIP << " and port:"
        << nDVRPort << " and login handle:" << lLoginID << endl;
}

//自动重连回调函数
void CALL_METHOD fHaveReConnectCB(LLONG lLoginID, char *pchDVRIP, LONG nDVRPort, LDWORD dwUser)
{
    cout << "ReConnect! IP:" << pchDVRIP << endl;
    return;
}

// 回调函数定义
void CALLBACK TimeDownLoadPosCallBack(LLONG lPlayHandle, DWORD dwTotalSize, DWORD
    dwDownLoadSize, int index, NET_RECORDFILE_INFO recordfileinfo, LDWORD dwUser)
{
    if(DAV_LENGTH * 100000 < dwTotalSize)
    {
        over = 1; //标志传输完成
        CLIENT_StopDownload(lDownloadHandle);
        //写日志
        //写日志
        sprintf(log_txt, "要下载的文件太大，文件大小为：%dB", dwTotalSize);
        write_log(log_txt);
    }
    // 若多个回放/下载使用相同的进度回调函数，则用户可通过 lPlayHandle 进行一一对应
    if (lPlayHandle == lDownloadHandle && dwTotalSize >= dwDownLoadSize)
    {
        //printf("lPlayHandle[%p]\n", lPlayHandle);
        printf("dwTotalSize[%d]\n", dwTotalSize);
        printf("dwDownLoadSize[%d]\n", dwDownLoadSize);
        //printf("index[%d]\n", index);
        //printf("dwUser[%p]\n", dwUser);
        printf("\n");
        downLoadSize = dwDownLoadSize;//downLoadSize 是一个全局变量，用于获取 当前下载到的 视频流的Size
    }
    if ((dwTotalSize - dwDownLoadSize) <= 1)
    {
        over = 1; //标志传输完成
    }
}

int CALLBACK DataCallBack(LLONG lRealHandle, DWORD dwDataType, BYTE *pBuffer, DWORD dwBufSize, LDWORD dwUser)
{
    int nRet = 0;
    // printf("call DataCallBack\n");
    // 若多个回放/下载使用相同的数据回调函数，则用户可通过 lRealHandle 进行一一对应
    if (lRealHandle == lDownloadHandle)
    {
        switch (dwDataType)
        {
        case 0:
            // Original data
            // 用户在此处保存码流数据，离开回调函数后再进行解码或转发等一系列处理
            if ((0 != lRealHandle) && (NULL != pBuffer) && (0 < dwBufSize))
            {
                FILE *fPic = fopen(file_name, "ab+");
                if (fPic)
                {
                    fwrite(pBuffer, 1, dwBufSize, fPic);
                    fclose(fPic);
                }
            }
            nRet = 1;//
            break;
        case 1:
            //Standard video data
            break;
        case 2:
            //yuv data
            break;
        case 3:
            //pcm audio data
            break;
        default:
            break;
        }
    }
    return nRet;
}



int main(void)
{
    while(1)
    {
        if(get_trigger("192.168.80.2"))
        //if(get_trigger(REDIS))
        {
            struct tm* time = get_time();
            //download_by_time("192.168.12.13", 37777, "admin", "aaa123456", 0, time);
            download_by_time("192.168.80.73", 37777, "admin", "aaa123456", 0, time);
            //download_by_time("192.168.80.73", 37777, "admin", "aaa123456", 1, time);
            //download_by_time("192.168.80.73", 37777, "admin", "aaa123456", 2, time);
            //download_by_time("192.168.80.73", 37777, "admin", "aaa123456", 3, time);
            //download_by_time("192.168.80.71", 37777, "admin", "aaa123456", 0, time);
            //download_by_time("192.168.80.72", 37777, "admin", "aaa123456", 0, time);
            //printf("按回车键继续执行程序……\r\n");
            //getchar();
        }
        sleep(3);
    }
    return 1;
}
