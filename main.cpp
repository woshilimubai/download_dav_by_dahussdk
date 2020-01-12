#include "dhnetsdk.h"
#include <QCoreApplication>
#include "stdio.h"
#include <iostream>
#include <unistd.h>
#include <hiredis/hiredis.h>
using namespace std;

LLONG lDownloadHandle = 0;
FILE *fPic;
BOOL over = 0;//用于标志 视频流是否传输结束，当over=0时，表示为传输结束；当over=1时，表示传输结束。
DWORD downLoadSize = 0;//用于统计 当前下载到的视频流的size

/**************download_by_time函数**************/
//功能：按时间下载IPC或NVR中的dav视频
//参数：ip_addr:设备IP地址；port:端口号；user:用户名；pwd：密码；channel_id:通道号；struct tm*:时间点；
//Note：通道号从0开始，IPC的通道号只能为0， NVR的通道号为0、1 、2、3……
//Note: stream_type: 0-主辅码流,1-主码流,2-辅码流
//返回值：下载成功 --> 1
int download_by_time(char* ip_addr, int port, char* user, char* pwd, unsigned char channel_id, struct tm* time)
{
    return 1;
}
/**************get_time函数******************/
// 功能：获取系统当前时间
// 参数：无
// 返回值：struct tm; tm->tm_year、tm_mon、tm_mday、tm_hour、tm_min、tm_sec
struct tm* get_time(void)
{
    //获取当前系统时间
    char datetime[50];
    time_t timep;
    struct tm *p_time;
    time (&timep);
    p_time = gmtime(&timep);
    return p_time;
}


/**************write_log函数******************/
// 功能：将程序中出现的异常信息写到log文件中
// 参数：异常信息，类型：字符串
// 返回值：写入成功 --> 1; 写入失败 --> 0。
int write_log(char *err)
{
    //获取当前系统时间
    char datetime[50];
    time_t timep;
    struct tm *p_time;
    time (&timep);
    p_time=gmtime(&timep);
    sprintf(datetime, "%d年%d月%d日 %d:%d:%d\t错误信息：%s\n", 1900 + p_time->tm_year, 1+p_time->tm_mon, p_time->tm_mday, 8+p_time->tm_hour, p_time->tm_min, p_time->tm_sec, err);
    //printf("%s\n", datetime);
    //将异常信息写到log文件中
    FILE *fp = fopen("error.log", "a");
    if(fp == NULL) //未存在error.log文件
    {
        fp = fopen("error.log", "w+");
    }
    fwrite(datetime, strlen(datetime), 1, fp);
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

    while(i <= 2)
    {
        connect = redisConnectWithTimeout(ip, 6379, timeout);
        if (!connect->err) //连接redis服务器成功
        {
            // 获取 trigger状态
            reply = (redisReply *)redisCommand(connect, "GET trigger_flag");
            if(reply->str == 0x0 || reply->str[0] == '1') //获取的trigger_flag符合规范
            {
                printf("GET trigger_flag: %s\n", reply->str);
                freeReplyObject(reply);
                break;
            }
            else //获取的trigger_flag不符合规范
            {
                freeReplyObject(reply);
                i++;
                //写日志
                write_log("获取 trigger_flag 状态失败");
                sleep(3);
            }
        }
        //连接redis服务器失败
       else
        {
           // printf("Connection error: %s\n", connect->errstr);
           i++;
           //写日志
           write_log("连接 redis服务器 失败");
           sleep(3);
       }
    }
    if(i == 3 || reply->str == "nil") //三次尝试获取trigger_flag均失败，或者 trigger_flag 的状态值为“nil”
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
        // 打印 回调数据的信息
        //printf("lPlayHandle[%p]\n", lRealHandle);
        //printf("dwDataType[%d]\n", dwDataType);
        //printf("pBuffer[%p]\n", pBuffer);
        //printf("dwBufSize[%d]\n", dwBufSize);
        //printf("dwUser[%p]\n", dwUser);
        //printf("\n");
        switch (dwDataType)
        {
        case 0:
            // Original data
            // 用户在此处保存码流数据，离开回调函数后再进行解码或转发等一系列处理
            if ((0 != lRealHandle) && (NULL != pBuffer) && (0 < dwBufSize))
            {
                FILE *fPic = fopen("test.dav", "ab+");
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

    get_trigger("127.0.0.1");
    unsigned char channel_id = 0;//NVR的通道，通道号从0开始，0--表示NVR通道一；1--表示NVR通道二
    int stream_type = 1; // 0-主辅码流,1-主码流,2-辅码流
    LLONG login_handle = 0;
    const char* ip_addr = "192.168.12.13";
    int port = 37777;
    const char* user = "admin";
    const char* pwd = "aaa123456";
    DWORD temp = 0;
    NET_DEVICEINFO_Ex info_ex = { 0 };
    int err = 0;
    CLIENT_Init(Disconnect, NULL);
    CLIENT_SetAutoReconnect(fHaveReConnectCB, NULL);
    login_handle = CLIENT_LoginEx2(ip_addr, port, user, pwd, (EM_LOGIN_SPAC_CAP_TYPE)0, NULL, &info_ex, &err);
    if (login_handle == 0)
    {
        printf("login error!\r\n");
    }
    else
    {
        printf("login success!\r\n");

        //****************开启录像下载***********************
        // 设置查询时的录像码流类型，此处设置码流类型为主码流
        CLIENT_SetDeviceMode(login_handle, DH_RECORD_STREAM_TYPE, &stream_type);
        NET_RECORDFILE_INFO info = { 0 };
        LPNET_TIME time_start = (LPNET_TIME)malloc(sizeof(LPNET_TIME));
        LPNET_TIME time_end = (LPNET_TIME)malloc(sizeof(LPNET_TIME));
        time_start->dwYear = 2020;
        time_start->dwMonth = 1;
        time_start->dwDay = 11;
        time_start->dwHour = 22;
        time_start->dwMinute = 10;
        time_start->dwSecond = 0;
        time_end->dwYear = 2020;
        time_end->dwMonth = 1;
        time_end->dwDay = 11;
        time_end->dwHour = 22;
        time_end->dwMinute = 30;
        time_end->dwSecond = 0;
        try
        {
            // 函数形参 sSavedFileName 和 fDownLoadDataCallBack 需至少有一个为有效值，否则入参有误
            lDownloadHandle = CLIENT_DownloadByTimeEx(login_handle, channel_id, EM_RECORD_TYPE_ALL, time_start, time_end, "test.dav", TimeDownLoadPosCallBack, NULL, DataCallBack, NULL);
            if (lDownloadHandle == 0)
            {
                printf("CLIENT_DownloadByTimeEx: failed! Error code: %x.\n", CLIENT_GetLastError());
            }
            else
            {
                //打开要下载的文件
                fPic = fopen("test.dav", "wb+");
                while (!over)
                {
                    sleep(1);//这里的延时并不会影响回调函数的正常执行
                    temp = downLoadSize;
                    sleep(1);
                    if (downLoadSize == temp)
                    {
                        break;//跳出这个while循环
                    }
                }
                fclose(fPic);
                //system("ffmpeg -i test.dav -c copy -map 0 test.avi -y");
                system("ffmpeg -i test.dav test.avi");
                CLIENT_StopDownload(lDownloadHandle);
                // Time = (double)cvGetTickCount() - Time;
                // printf("run time = %gms\n", Time / (cvGetTickFrequency() * 1000));
            }
        }
        catch (double d)
        {
            cout << "catch(double) " << d << endl;
        }
    }
    // 释放网络库
    CLIENT_Logout(login_handle);
    CLIENT_Cleanup();
    printf("按回车键退出……\r\n");
    getchar();
    printf("程序运行结束！！！！！！！！");
    return 1;
}
