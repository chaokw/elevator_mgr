#ifndef	BLE_HANDLER_H
#define	BLE_HANDLER_H

#include <time.h>
#include "ble_mgr.h"

#undef  SUCCESS
#define SUCCESS  0

#undef  FAILURE
#define FAILURE  1

#define TRUE  1
#define FALSE 0

#define WANIDLE  0
#define WANUP    1
#define WANDOWN  2

#define MAX_SEND_BUFF (1024)
#define MAX_RECV_BUFF (1024)

#define DEVSOP 0xfe

#define DFMZ_PLATFORM_URI "http://219.233.18.245/connector/otter_elevator"
#define TIGERCEL_LINKUP_URI "http://x-core.otterai.tigercel.com.cn/dev/status"
#define TIGERCEL_MONITOR_URI "http://x-core.otterai.tigercel.com.cn/rest/v1/dev/elev/%s/status"
#define TIGERCEL_FAULT_URI "http://x-core.otterai.tigercel.com.cn/rest/v1/dev/elev/%s/alarm"
#define ONENET_PLATFORM_URI "http://api.heclouds.com/devices/29699307/datapoints?type=3"

#define JSON_FMT  "{\"dev_id\":\"%s\", \"time\":%d, \"seq\":%d, \"data\":\"%s\"}"
#define DFMZ_JSON_FMT  "{\"dev_id\":\"%s\", \"time\":%d, \"seq\":%d, \"data\":\"%s\"}"
#define TIGERCEL_LINKUP_JSON_FMT  "{\"devid\":\"%s\",\"mtu\":1500,\"modules\":[{\"name\":\"IF812-1\",\"hwver\":\"WG256\",\"swver\":\"V01.01.00\"}],\"location\":{\"GPRS\":{\"lac\":10000,\"cid\":4000}}}"  //chaokw
#define TIGERCEL_MONITOR_JSON_FMT  "{\"direction\":%d, \"door\":%d, \"floor\":%d, \"speed\":%f}"
#define TIGERCEL_FAULT_JSON_FMT  "{\"fault\":%d, \"direction\":%d, \"floor\":%d, \"speed\":%f}"
//#define ONENET_JSON_FMT  "{\"datastreams\":[{\"id\":\"info\",\"datapoint\":[{\"devid\":\"%s\"}]},{\"id\":\"status\",\"datapoints\":[{\"direction\":%d, \"door\":%d, \"floor\":%d, \"speed\":%f}]}]}"
#define ONENET_JSON_FMT "{\"devid\":\"%s\", \"fault\":%d, \"direction\":%d, \"door\":%d, \"floor\":%d, \"people\":%d, \"speed\":%f}"

/*****************************************************************************
* Common Response types
*****************************************************************************/
typedef enum MsgRespT
{
    RESP_OK          =  0,  
    RESP_ERROR       = -1,  
} MsgRespT;


/*****************************************************************************
 * Common Response Header Format
 *****************************************************************************/
#if 0
typedef struct CommonRspHdrT 
{
    CompMsgHeaderT   msgHdr;     /* Common Message Header */
    unsigned char      code;       /* Return Code */
} CommonRspHdrT;
#endif


typedef struct {
    unsigned char sop;
    unsigned char len;
    unsigned char type;
    unsigned char addr;
    unsigned char cmd;
    unsigned char data[256];
}dev_AS_cmd_t;


typedef struct {
    int direction;
    int door;
    int floor;	
    int fault;
    int people;
    float speed;
}elevator_status_t;


typedef struct
{
    char dev_id[32];
    int time;
    int seq;
    char data[256];
}elevator_info_t;


typedef struct _t_http_post{
    unsigned int clock;
    unsigned int result;
    char *url;
}http_post;


#endif /* #ifndef BLE_HANDLER_H */
