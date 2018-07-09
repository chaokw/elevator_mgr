/* --- system includes ---*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <curl/curl.h>
#include <net/if.h>
#include <time.h>


/* --- project includes ---*/
#include "ble_mgr.h"
#include "ble_handler.h"
#include "cJSON.h"
#include "gpiohandle.h"


#define  RS485DATAFILE        "/var/rs485_data"
#define setbit(x,y)  x|=(1<<y)
#define h_assert(res)   \
if(res)\
{\
        fprintf(stderr, "[error] <%s - %d> : %s => %d\n", __FILE__, __LINE__, curl_easy_strerror(res), res);\
        goto END;\
}


/* --- local static constants ---*/
static unsigned char rxbuf[256];
static unsigned char received_buffer[GLOBAL_RECEIVE_BUFFER_SIZE];
char rs485_json_data[256] = {0};  //chaokw
dev_AS_cmd_t dev_AS_buf = {0};
elevator_status_t elevator_status = {0};

http_post *php = NULL;
static int data_sequence=1;

extern BLEMgmtTaskCtxT  *gpBLEMgmtTaskCtx;
static const char *base64char = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


static void print_hex_line(unsigned char *prefix, unsigned char *outbuf, int index)
{
  int i;

  printf("\r%s", prefix);
  for(i = 0; i < index; i++) {
    if((i % 4) == 0) {
      printf(" ");
    }
    printf("%02X", outbuf[i] & 0xFF);
  }
  printf("  ");
  for(i = index; i < HCOLS; i++) {
    if((i % 4) == 0) {
      printf(" ");
    }
    printf("  ");
  }
  for(i = 0; i < index; i++) {
    if(outbuf[i] < 30 || outbuf[i] > 126) {
      printf(".");
    } else {
      printf("%c", outbuf[i]);
    }
  }
}

int getIfMac(char *ifname, char *if_hw)
{
    struct ifreq ifr;
    char *ptr;
    int skfd;

    if((skfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        return -1;
    }
    strncpy(ifr.ifr_name, ifname, IF_NAMESIZE);
    if(ioctl(skfd, SIOCGIFHWADDR, &ifr) < 0) {
        close(skfd);
        return -1;
    }
    ptr = (char *)&ifr.ifr_addr.sa_data;
    sprintf(if_hw, "%02X%02X%02X%02X%02X%02X",
            (ptr[0] & 0377), (ptr[1] & 0377), (ptr[2] & 0377),
            (ptr[3] & 0377), (ptr[4] & 0377), (ptr[5] & 0377));
    close(skfd);
    return 0;
}



int chkWanStatus()
{
        int ret = -1;
        FILE *fp;
        char result[64] = {0};

        fp = fopen("/tmp/wan_status", "r");
        if (NULL == fp) {
                ret = WANDOWN;
        }
        else {
                fgets(result, sizeof(result), fp);
                fclose(fp);

                if(strstr(result, "up") || strstr(result, "conn")) {
                        ret = WANUP;
                }
                else if(strstr(result, "down") || strstr(result, "dis")){
                        ret = WANDOWN;
                }
        }
        return ret;
}

int chk3GStatus()
{
        int ret = -1;
        FILE *fp;
        char result[64] = {0};

        fp = fopen("/var/3gstat", "r");
        if (NULL == fp) {
                ret = WANDOWN;
        }
        else {
                fgets(result, sizeof(result), fp);
                fclose(fp);

                if(strstr(result, "up") || strstr(result, "conn")) {
                        ret = WANUP;
                }
                else if(strstr(result, "down") || strstr(result, "dis")){
                        ret = WANDOWN;
                }
        }
        return ret;
}




char *base64_encode(const unsigned char *bindata, char *base64, int binlength)
{
    int i, j;
    unsigned char current;
    for (i = 0, j = 0 ; i < binlength ; i += 3) {
        current = (bindata[i] >> 2) ;
        current &= (unsigned char)0x3F;
        base64[j++] = base64char[(int)current];
        current = ((unsigned char)(bindata[i] << 4)) & ((unsigned char)0x30) ;
        if (i + 1 >= binlength) {
            base64[j++] = base64char[(int)current];
            base64[j++] = '=';
            base64[j++] = '=';
            break;
        }
        current |= ((unsigned char)(bindata[i+1] >> 4)) & ((unsigned char)0x0F);
        base64[j++] = base64char[(int)current];
        current = ((unsigned char)(bindata[i+1] << 2)) & ((unsigned char)0x3C) ;
        if (i + 2 >= binlength) {
            base64[j++] = base64char[(int)current];
            base64[j++] = '=';
            break;
        }
        current |= ((unsigned char)(bindata[i+2] >> 6)) & ((unsigned char)0x03);
        base64[j++] = base64char[(int)current];
        current = ((unsigned char)bindata[i+2]) & ((unsigned char)0x3F) ;
        base64[j++] = base64char[(int)current];
    }
    base64[j] = '\0';
    return base64;
}


int base64_decode(const char *base64, unsigned char *bindata)
{
    int i, j;
    unsigned char k;
    unsigned char temp[4];
    for (i = 0, j = 0; base64[i] != '\0' ; i += 4 ) {
        memset(temp, 0xFF, sizeof(temp));
        for (k = 0; k < 64; k++) {
            if (base64char[k] == base64[i]) {
                temp[0]= k;
            }
        }
        for (k = 0; k < 64; k++) {
            if (base64char[k] == base64[i+1]) {
                temp[1]= k;
            }
        }
        for (k = 0; k < 64; k++) {
            if (base64char[k] == base64[i+2]) {
                temp[2]= k;
            }
        }
        for (k = 0; k < 64; k++) {
            if (base64char[k] == base64[i+3]) {
                temp[3]= k;
            }
        }
        bindata[j++] = ((unsigned char)(((unsigned char)(temp[0] << 2))&0xFC)) |
                ((unsigned char)((unsigned char)(temp[1]>>4)&0x03));
        if (base64[i+2] == '=') {
            break;
        }
        bindata[j++] = ((unsigned char)(((unsigned char)(temp[1] << 4))&0xF0)) |
                ((unsigned char)((unsigned char)(temp[2]>>2)&0x0F));
        if (base64[i+3] == '=') {
            break;
        }

        bindata[j++] = ((unsigned char)(((unsigned char)(temp[2] << 6))&0xF0)) |
                ((unsigned char)(temp[3]&0x3F));
    }
    return j;
}



/*****************************************************************************
* Name:MsgProcInit
* Description:sets up the BLE management message process thread configuration, 
*                  and initializes anything else needed by this. 
* Input param:
* Output param:
* Return-Value:
* MSG_OK - Function completed successfully
* MSG_ERROR - Memory allocation failure occurred
******************************************************************************/ 
#if 0
MsgErrorT MsgProcInit(void)
{
    MsgErrorT rc = MSG_OK;

    do
    {
        /* Initialize the BLEMgmt message queue. */
        rc = MsgQInit(&gpBLEMgmtTaskCtx->msgq, MSGQ_ID_AP_MANAGEMENT);
        if (rc != MSG_OK)
        {
            printf("MsgQInit failed, rc=%d.\n", rc);
            break;
        }
    } while(0);

    return rc;
}
#endif


/*****************************************************************************
* Name:BLEProcThread
* Description:This function sets up the message process thread configuration, then
*                  enters the main thread loop.
* Input param:
* ptr - Unused argument (e.g., UNUSED_ARG)
* Output param:
* Return-Value:
******************************************************************************/
void *BLEProcThread( UNUSED_ARG void *ptr )
{   
    /* do some init, such as message queue init ... */
    //MsgProcInit();
    BLEProcMain(gpBLEMgmtTaskCtx);
    return NULL;
}


unsigned char checksum_8(unsigned char *pData, unsigned char len)  
{  
	unsigned char sum = 0;
	unsigned char i;
	
	for (i=0; i < len; i++) {
		sum += pData[i];
	}
	return sum;
}  

int json_data_parse(char *jsonBody)
{
	cJSON *type;
	cJSON *stype;
	cJSON *now_status;
	cJSON *direction;
	cJSON *door_status;
	cJSON *has_people;
	cJSON *now_floor;
	cJSON *fault_type;
	cJSON *floor;
	cJSON *speed;
	
	cJSON *root = cJSON_Parse(jsonBody);  

	if (!root){  
		printf("\r\ncJSON_Parse error before: [%s]\n",cJSON_GetErrorPtr());	
		return FAILURE;
	} 

	type = cJSON_GetObjectItem(root,"type"); 
	if(type != NULL) {
	  if (strcmp("Monitor",type->valuestring) == 0) {
		now_status = cJSON_GetObjectItem(root,"now_status");
		direction = cJSON_GetObjectItem(root,"direction");
		door_status = cJSON_GetObjectItem(root,"door_status");
		has_people = cJSON_GetObjectItem(root,"has_people");
		now_floor = cJSON_GetObjectItem(root,"now_floor");		
		speed = cJSON_GetObjectItem(root,"speed");

		dev_AS_buf.sop = 0xFE;
		dev_AS_buf.len = 0x16;
		dev_AS_buf.type = 0x01;
		dev_AS_buf.addr = 0x0A;
		dev_AS_buf.cmd = 0x01;

		dev_AS_buf.data[0] = 0x08;
		dev_AS_buf.data[1] = 0xc0;
		
		if(now_status != NULL) {	
			if(1 == now_status->valueint) {
				dev_AS_buf.data[6] = 0x01;
			}		
			else if(2 == now_status->valueint || 3 == now_status->valueint) {
				dev_AS_buf.len = 0x01;
				dev_AS_buf.cmd = 0x03;
				dev_AS_buf.data[0] = 0x08;
				if(2 == now_status->valueint)
					elevator_status.fault = 7;
				else if(3 == now_status->valueint)
					elevator_status.fault = 8;
				goto CHKSUM;
			}
		}
		if(direction != NULL) {	
			if(1 == direction->valueint) {
				setbit(dev_AS_buf.data[0], 0);
			    elevator_status.direction = 1;
			} else if(2 == direction->valueint) {
				setbit(dev_AS_buf.data[0], 1);
			    elevator_status.direction = -1;
			} else if(0 == direction->valueint) {
				setbit(dev_AS_buf.data[0], 1);
			    elevator_status.direction = 0;
			}
		}
		if(door_status != NULL) {	
			if(0 == door_status->valueint || 2 == door_status->valueint) {
				setbit(dev_AS_buf.data[1], 5);
				elevator_status.door = 0;
			} else if(1 == door_status->valueint || 3 == door_status->valueint) {
				setbit(dev_AS_buf.data[1], 4);
				elevator_status.door = 1;
			}
		}
		if(has_people != NULL) {	
			elevator_status.people = has_people->valueint;
		}
		if(now_floor != NULL) {	
			dev_AS_buf.data[4] = now_floor->valueint;
			elevator_status.floor = now_floor->valueint;
		}
		if(speed != NULL) {	
			elevator_status.speed = speed->valuedouble;  //chaokw
		}
		
CHKSUM: dev_AS_buf.data[dev_AS_buf.len] = checksum_8((unsigned char *)&dev_AS_buf.type,dev_AS_buf.len + 3);
		
	  }
	  else if (strcmp("Fault",type->valuestring) == 0) {
	 	stype = cJSON_GetObjectItem(root,"stype");
	  if(stype != NULL) {
     		if (strcmp("add",stype->valuestring) == 0) {
     		 	dev_AS_buf.sop = 0xFE;
     			dev_AS_buf.len = 0x0A;
     			dev_AS_buf.type = 0x01;
     			dev_AS_buf.addr = 0x0A;
     			dev_AS_buf.cmd = 0x05;
     			dev_AS_buf.data[0] = 0x01;
     			
     			fault_type = cJSON_GetObjectItem(root,"fault_type");
     			floor = cJSON_GetObjectItem(root,"floor");
     			direction = cJSON_GetObjectItem(root,"direction");
				speed = cJSON_GetObjectItem(root,"speed");
     			
     			if(fault_type != NULL) {	
     				dev_AS_buf.data[1] = fault_type->valueint / 256;
     				dev_AS_buf.data[2] = fault_type->valueint % 256;
				    elevator_status.fault = fault_type->valueint;
     			}
     			if(floor != NULL) {	
     				dev_AS_buf.data[3] = floor->valueint;
					elevator_status.floor = floor->valueint;
     			}
			    if(direction != NULL) {	
			        if(0 == direction->valueint) {
			          elevator_status.direction = -1;
			        } else if(1 == direction->valueint) {
			          elevator_status.direction = 1;
			        } 
			    }
				if(speed != NULL) {	
			        elevator_status.speed = speed->valuedouble;  //chaokw
		        }
     
     			dev_AS_buf.data[dev_AS_buf.len] = checksum_8((unsigned char *)&dev_AS_buf.type,dev_AS_buf.len + 3);
     			
     		}
        }
	  }
	}

	cJSON_Delete(root); 
	return SUCCESS;
}


int data2file(char *file, char *jsonBody)
{
	char buf[256];
	time_t timep;
	
    FILE *fd = fopen(file,"w+");
    if(!fd) return -1;
    time (&timep);
	
	sprintf(buf, "%s-->%s", ctime(&timep), jsonBody);
    fwrite(buf, strlen(buf),1,fd);
    fclose(fd);
    return 0;
}


int rs485_push_buff(unsigned char uartDat)
{
	int ret = 0;
	static unsigned char count = 0;
	static unsigned char uartBuf[256] = {0};
	int i;
	
	uartBuf[count] = uartDat;
	if (uartBuf[0] == '{') {
    	    count ++;
	    if (uartBuf[count - 1] == '}') {
		memset(rs485_json_data,0,256);
		memcpy(rs485_json_data, uartBuf, count);
		rs485_json_data[count] = '\0';

		if(ENABLE == gpBLEMgmtTaskCtx->cfg.debug ) {
		  for (i=0; i<count; i++)
         		printf("%c", rs485_json_data[i]);
			printf("\n");
		}

        //LedCtrl(GPIO_PHONELED, GPIO_LEDON);
        gpioLedSet(GPIO_PHONELED,0,1,0,1,1);
		data2file(RS485DATAFILE, rs485_json_data);
		json_data_parse(rs485_json_data);
        //LedCtrl(GPIO_PHONELED, GPIO_LEDOFF);

		count = 0;
		memset(uartBuf, 0, sizeof(uartBuf));
	    }

            if (count > 253) {
		count = 0;
        	memset(uartBuf, 0, sizeof(uartBuf));
	    }	
		
	} else {
	    count = 0;
            memset(uartBuf, 0, sizeof(uartBuf));
	}

	return ret;
}


/*****************************************************************************
* Name:BLEProcMain
* Description:This function is main thread loop of message processing. 
* Input param:
* BLEMgmt - Pointer to the BLEMgmtTaskCtxT
* Output param:
* Return-Value:
* MSG_OK - if everything worked as expected
* MSG_ERROR - if an error occurs
******************************************************************************/
BLEErrorT BLEProcMain(BLEMgmtTaskCtxT *BLEMgmt)
{
    BLEErrorT rc;
    //MsgQMessageT *msg = NULL;
    int nfound;
    int index = 0;	
    unsigned char buf[BUFSIZE], outbuf[HCOLS];
    char *timeformat = TIMEFORMAT;
    unsigned char uartDat;  //chaokw
	
    if (NULL == BLEMgmt)
    {
        printf("elevator mgmt is exiting due to invalid control context.\n");
        return BLE_ERROR;
    }

    /* Main BLEMgmt Loop */
    printf("elevator mgmt process is running.\n");

    while (BLEMgmt->running == TRUE)
    {
       nfound = select(FD_SETSIZE, &gpBLEMgmtTaskCtx->uart.mask, (fd_set *) 0, (fd_set *) 0, (struct timeval *) 0);
       if(nfound < 0) {
         if(errno == EINTR) {
           fprintf(stderr, "interrupted system call\n");
           continue;
         }
         perror("select");
         exit(1);
       }

       if(FD_ISSET(gpBLEMgmtTaskCtx->uart.fd, &gpBLEMgmtTaskCtx->uart.mask)) {
         /* Get buffer */
         int i, j, n = read(gpBLEMgmtTaskCtx->uart.fd, &uartDat, 1);
         if(n < 0) {
           perror("could not read");
           exit(-1);
         }

         /* If debug is enabled, print uart data */
         if(ENABLE == gpBLEMgmtTaskCtx->cfg.debug ) {
              for(i = 0; i < n; i++) {
                switch(gpBLEMgmtTaskCtx->uart.mode) {
                  case MODE_START_DATE: {
                    time_t t;
                    t = time(&t);
                    strftime(outbuf, HCOLS, timeformat, localtime(&t));
                    printf("%s|", outbuf);
                    gpBLEMgmtTaskCtx->uart.mode = MODE_DATE;
                  }
                  case MODE_DATE:
                    printf("%c", buf[i]);
                    if(buf[i] == '\n') {
                      gpBLEMgmtTaskCtx->uart.mode = MODE_START_DATE;
                    }
                    break;
                  case MODE_HEX:
                    //rxbuf[index++] = buf[i];
                    rxbuf[index++] = uartDat;  //chaokw
                    if(index >= HCOLS) {
                      print_hex_line("", rxbuf, index);
                      index = 0;
                      printf("\n");
                    }
                    break;
                }
              }
          }

          /* received content  */  
          //L1_receive_data(buf, n);
          
          rs485_push_buff(uartDat);

         }
    }

    /* Clean up the message queue */
    //MsgQShutdown(&BLEMgmt->msgq);

    /* Thread is exiting */
    printf("elevator mgmt message process is exiting.\n");
    return BLE_OK;
}


static int sleep_with_restart(int second)
{
    int left;

    left = second;
    while (left > 0)
            left = sleep(left);
    return 0;
}


static size_t callback_rd(void *ptr, size_t size, size_t nmemb, void *userp)
{
    if(strlen(userp) + strlen(ptr) < MAX_RECV_BUFF)
        strcat(userp, ptr);
    else
        return 1;
    return size * nmemb;
}


void process_respone(char *recv_buffer)
{
        cJSON *root;
        char *ptr = recv_buffer;

        printf("%s\n", recv_buffer);
        root = cJSON_Parse(ptr);
        if (!root) {
            return;
        }
        cJSON *code = cJSON_GetObjectItem(root, "resultCode");
        if(code) {
            printf("resultCode : [%s]\n", code->valuestring);
        }
        else {
            printf("resultCode : [null]\n");
        }
        cJSON *msg = cJSON_GetObjectItem(root, "resultMessage");
        if(msg) {
            printf("resultMessage : [%s]\n", msg->valuestring);
        }
        else {
            printf("resultMessage : [null]\n");
        }
        cJSON_Delete(root);
}


int get_imei(char *file, char *imei, int len)
{
    FILE *fp;
    char result[256]={0};
    char imei_ret[20]={0};
    char *imei_str = NULL;
    fp = fopen(file, "r");
    if(fp){
        while(fgets(result, sizeof(result), fp)){
            imei_str = strstr(result, "imei");
            if(imei_str){
                break;
            }
        }
        if(imei_str){
            imei_str = imei_str + 5;//really IMEI
            strncpy(imei_ret, imei_str, 15);
        }
        fclose(fp);

        if(strlen(imei_ret)){
            strcpy(imei, imei_ret);
        }
    }else{
       strcpy(imei, "");
       return -1;
    }
    return 0;
}


/*
{
	"dev_id":"xxxxxxxxxxxxxxx", //device imei
	"time":xxxxxxxx, //4bytes, unit:second
	"seq":xxxxxxxx, //4bytes, start from 1
	"data":"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" //base64 encoded data
} 
*/
void post_data(CURL *curl, char *send_buffer, const int buff_size, char *recv_buffer)
{
    cJSON *params = NULL;
    elevator_info_t elevator_info;
    CURLcode res;
    time_t curtime;
    char buf[64] = "";
    int ret = 0;

    if (dev_AS_buf.sop == DEVSOP) {
        elevator_info.time = time(NULL);
        elevator_info.seq = data_sequence;

        params = cJSON_CreateObject();

        memset(buf,0,sizeof(buf));
        ret = get_imei("/var/moduleinfofile", buf, sizeof(buf));
        if(ret < 0) {
            printf("get imei fail, using mac as dev_id!\n");
            getIfMac("br0", buf);
        } else {
        }

        base64_encode(&dev_AS_buf, &elevator_info.data, dev_AS_buf.len + 6);
        memset(send_buffer, 0, buff_size);

        sprintf(send_buffer, JSON_FMT, buf, elevator_info.time, elevator_info.seq, &elevator_info.data);  //chaokw

        if(ENABLE == gpBLEMgmtTaskCtx->cfg.debug ) {
	        printf("%s\n", send_buffer);
        }

        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(send_buffer));  //chaokw
        res = curl_easy_perform(curl);
        if(res != CURLE_OK && res != CURLE_WRITE_ERROR) {
            printf("%s => %d\n", curl_easy_strerror(res), res);
            return;
        }
        process_respone(recv_buffer);

    }
}


/*
DFMZ platform
{
	"dev_id":"xxxxxxxxxxxxxxx", //device imei
	"time":xxxxxxxx, //4bytes, unit:second
	"seq":xxxxxxxx, //4bytes, start from 1
	"data":"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" //base64 encoded data
} 

Tigecel platform linkup
{
	"devid": "xxxxxxxxxxxxxxx",
	"mtu": 1500,
	"modules": [{
		"name": "IF812-1",
		"hwver": "WG256",
		"swver": "V01.00.00"
	}],
	"location": {
		"GPRS": {
			"lac": 10000,
			"cid": 4000
		}
	}
}

Tigecel platform monitor
{
	"direction": 1,
	"door": 0,
	"floor": 3,
	"speed": 5
}

Tigecel platform fault
{
	"fault": 1,		
	"direction": 1,
	"floor": 3,
	"speed": 5
}

*/
void post_data_2(CURL *curl, char *send_buffer, char *fmt, const int buff_size, char *recv_buffer)
{
    cJSON *params = NULL;
    elevator_info_t elevator_info;
    CURLcode res;
    time_t curtime;
    char buf[64] = "";
    int ret = 0;

    if (dev_AS_buf.sop == DEVSOP || strcmp(fmt, TIGERCEL_LINKUP_JSON_FMT) == 0) {
        elevator_info.time = time(NULL);
        elevator_info.seq = data_sequence;

        params = cJSON_CreateObject();

        memset(buf,0,sizeof(buf));
        ret = get_imei("/var/moduleinfofile", buf, sizeof(buf));
        if(ret < 0) {
            printf("get imei fail, using mac as dev_id!\n");
            getIfMac("br0", buf);
        } 

        memset(send_buffer, 0, buff_size);

        if(strcmp(fmt, DFMZ_JSON_FMT) == 0) {
            base64_encode(&dev_AS_buf, &elevator_info.data, dev_AS_buf.len + 6);
            sprintf(send_buffer, fmt, buf, elevator_info.time, elevator_info.seq, &elevator_info.data);
        } 
        else if(strcmp(fmt, TIGERCEL_LINKUP_JSON_FMT) == 0) {
            sprintf(send_buffer, fmt, buf);
        }
        else if(strcmp(fmt, TIGERCEL_FAULT_JSON_FMT) == 0) {
            sprintf(send_buffer, fmt, elevator_status.fault, elevator_status.direction, elevator_status.floor, elevator_status.speed);
        }
        else if(strcmp(fmt, TIGERCEL_MONITOR_JSON_FMT) == 0) {
            sprintf(send_buffer, fmt, elevator_status.direction, elevator_status.door, elevator_status.floor, elevator_status.speed);
        }
        else if(strcmp(fmt, ONENET_JSON_FMT) == 0) {
            sprintf(send_buffer, fmt, buf, elevator_status.fault, elevator_status.direction, elevator_status.door, elevator_status.floor, elevator_status.people, elevator_status.speed);
        }

        if(ENABLE == gpBLEMgmtTaskCtx->cfg.debug ) {
	        printf("%s\n", send_buffer);
        }

        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(send_buffer)); 
        res = curl_easy_perform(curl);
        if(res != CURLE_OK && res != CURLE_WRITE_ERROR) {
            printf("%s => %d\n", curl_easy_strerror(res), res);
            return;
        }
        gpioLedSet(GPIO_SIGNALRED,0,1,0,1,1);
        process_respone(recv_buffer);

    }
}


void PostData2Server(void)
{

    CURL *curl;
    CURLcode res;
    void *recv_buffer = NULL;
    char *send_buffer = NULL;
    struct curl_slist *chunk = NULL;
    char dev[64] = "";
	char url[128] = "";
	int status;

    while( WANUP != chk3GStatus() && WANUP != chkWanStatus()) {
        sleep(3);
    }
	printf("%s  %d ,WAN status OK\n", __FILE__,__LINE__);

    send_buffer = (char *)malloc(MAX_SEND_BUFF);
    if(!send_buffer) {
            fprintf(stderr, "[error] <%s - %d> : %s\n", __FILE__, __LINE__, strerror(errno));
            return;
    }
    memset(send_buffer, 0, MAX_SEND_BUFF);


    recv_buffer = (void *)malloc(MAX_RECV_BUFF * sizeof(char));
    if(!recv_buffer) {
            free(send_buffer);
            fprintf(stderr, "[error] <%s - %d> : %s\n", __FILE__, __LINE__, strerror(errno));
            return;
    }
    memset(recv_buffer, 0, MAX_RECV_BUFF);
    
    php = (http_post *)calloc(1, sizeof(http_post));
    php->clock = 5;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if(curl) {      
        chunk = curl_slist_append(NULL,"Content-Type:application/json;charset=UTF-8");
        //chunk = curl_slist_append(chunk,"api-key: xstv2FIguE=eSZwSkYCXDdOWavw=");   
        res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
        h_assert(res);

        res = curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2);
        h_assert(res);

        res = curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3);
        h_assert(res);

        res = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
        h_assert(res);

        res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, callback_rd);
        h_assert(res);

        res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, recv_buffer);
        h_assert(res);

        res = curl_easy_setopt(curl, CURLOPT_POST, 1); 
        h_assert(res);

        res = curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
        h_assert(res);

        res = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, send_buffer);
        h_assert(res);

        /* online msg to tigercel server */  
        curl_easy_setopt(curl, CURLOPT_URL, TIGERCEL_LINKUP_URI);
        curl_easy_setopt(curl, CURLOPT_PORT, 8810);
        post_data_2(curl, send_buffer, TIGERCEL_LINKUP_JSON_FMT, MAX_SEND_BUFF, recv_buffer);

        for(;;) {
                sleep_with_restart(php->clock);
                memset(recv_buffer, 0, MAX_RECV_BUFF);

                /****** DFMZ PLATFORM ******/
                curl_easy_setopt(curl, CURLOPT_URL, DFMZ_PLATFORM_URI);
                curl_easy_setopt(curl, CURLOPT_PORT, 15329);
                post_data_2(curl, send_buffer, DFMZ_JSON_FMT, MAX_SEND_BUFF, recv_buffer);
				
                //curl_easy_setopt(curl, CURLOPT_PORT, 17329);
                curl_easy_setopt(curl, CURLOPT_PORT, 17344);
                post_data_2(curl, send_buffer, DFMZ_JSON_FMT, MAX_SEND_BUFF, recv_buffer);

                /****** TIGERCEL PLATFORM ******/
                memset(dev,0,sizeof(dev));
                status = get_imei("/var/moduleinfofile", dev, sizeof(dev));
                if(status < 0) {
                    getIfMac("br0", dev);
                } 
#if 1				
                if(elevator_status.fault != 0) {
                    sprintf(url, TIGERCEL_FAULT_URI, dev);
                    php->url = strdup(url);
                    printf("1:php->url=%s\n",php->url);
                    curl_easy_setopt(curl, CURLOPT_URL, php->url);
                    curl_easy_setopt(curl, CURLOPT_PORT, 8600);
                    post_data_2(curl, send_buffer, TIGERCEL_FAULT_JSON_FMT, MAX_SEND_BUFF, recv_buffer);
                } else {
                    sprintf(url, TIGERCEL_MONITOR_URI, dev);
                    php->url = strdup(url);
                    curl_easy_setopt(curl, CURLOPT_URL, php->url);
                    curl_easy_setopt(curl, CURLOPT_PORT, 8600);
                    post_data_2(curl, send_buffer, TIGERCEL_MONITOR_JSON_FMT, MAX_SEND_BUFF, recv_buffer);
                }
#endif

#if 0
                /****** ONENET PLATFORM ******/
                curl_easy_setopt(curl, CURLOPT_URL, ONENET_PLATFORM_URI);
                curl_easy_setopt(curl, CURLOPT_PORT, 80);
                post_data_2(curl, send_buffer, ONENET_JSON_FMT, MAX_SEND_BUFF, recv_buffer);
#endif

                if (dev_AS_buf.sop == DEVSOP) 
                  data_sequence++;
                memset((void *)&dev_AS_buf, 0, sizeof(dev_AS_cmd_t));
                memset((void *)&elevator_status, 0, sizeof(elevator_status_t));
        }
        curl_easy_cleanup(curl);
    }

END:
    free(send_buffer);
    free(recv_buffer);
    free(php->url);
    free(php);
    curl_global_cleanup();
    return;
}



