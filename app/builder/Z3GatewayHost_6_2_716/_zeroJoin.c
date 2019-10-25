#include "app/framework/include/af.h"
#include "app/framework/util/af-main.h"
#include "app/framework/util/attribute-storage.h"
#include "./common.h"
#include "cJSON.h"

// Zero 模式channel
#define _ZERO_CHANNEL			18

// 设备维护限制时间（单位0.1s）
#define _LIMIT_TIMEOUT_			50
// 最小启动入网延时（单位0.1s）
#define _LIMIT_MINDELAY_JOIN	50
// 最大启动入网延时（单位0.1s）
#define _LIMIT_MAXDELAY_JOIN	240

// MQTT Topic 
#define _MQTT_TOPIC_ZERO_		"simonZero"

// Simon 固定头
static const uint8_t SIMON_HEADER[] = {0xEF, 0xAB, 0xCD, 0x12, 0x34};

// 设备info结构定义
typedef struct DEVICEINFOLIST{
	struct DEVICEINFOLIST *next;
	uint32_t timeout;
	uint8_t eui64[8];
	uint8_t modelId[64];
	bool sendFlag;
	uint8_t sendDelay;	// 50~255
}_deviceInfoList_t;

// 设备info列表
LIST(_devInfo_list);

// 正式网络参数
static uint8_t __radio_channel = 0xFF;
static uint16_t __panId = 0xFFFF;

static uint32_t _time_com = 0;
static uint32_t _time_1S = 0;

// mfg开关状态
static uint8_t _run_ = 0;


void _Zero_deviceListPrint(void);
int8_t _Zero_checkAndPublishMqttSimonZero(void);
void _carel_GateWayGetNetworkParameter(uint8_t *channel, uint16_t *panId);
int8_t _zero_TxSimonMessage_ControlJoin(_deviceInfoList_t *item);




// 周期0.1s
EmberEventControl _zeroJoinTxRx_EC;
void _zeroJoinTxRx_EF(void){
	emberEventControlSetInactive(_zeroJoinTxRx_EC);

	static uint16_t _cnt_com1 = 0;
	static uint16_t _cnt_com2 = 0;
	static uint16_t _cntGoal_com2 = 0;

	if(++_cnt_com1 > 10){
		_cnt_com1 = 11;
	}
	// 1s事件
	if(_cnt_com1 == 10){
		// 初始化info列表
		list_init(_devInfo_list);
		// 使能MFG-lib
		extern uint8_t _carel_mfg_enable;
		_carel_mfg_enable = 1;
		//uint8_t enabled = 1;
		//halCommonSetToken(TOKEN_MFG_LIB_ENABLED, &enabled);

		extern EmberEventControl _zeroJoinManage_EC;
		emberEventControlSetDelayMS(_zeroJoinManage_EC, 10);
#if 1
		// TODO (临时， 在网络创建完毕也应该调用)
		if(emberAfNetworkState() == EMBER_JOINED_NETWORK){
			_carel_GateWayGetNetworkParameter(&__radio_channel, &__panId);
			emberAfCorePrint("\n\n\n");
			emberAfCorePrint("############### [Zero]get NWK parameter, channel : %d ,  panId : 0x%2X ###############\n", __radio_channel, __panId);
			emberAfCorePrint("\n\n\n");
		}
		else{
			// nothing
		}
#endif
	}


	// mfg开关处理状态机
	static uint8_t __seq_enable__ = 0;
	switch(__seq_enable__){
		// disable
		case 0:
			if(_run_ > 0){
#if 1
				// 获取网络状态
				if(emberAfNetworkState() == EMBER_JOINED_NETWORK){
					_carel_GateWayGetNetworkParameter(&__radio_channel, &__panId);
					emberAfCorePrint("\n\n\n");
					emberAfCorePrint("############### [Zero]get NWK parameter, channel : %d ,  panId : 0x%2X ###############\n", __radio_channel, __panId);
					emberAfCorePrint("\n\n\n");
				}
				else{
					_run_ = 0;
					break;
				}
#endif
				// 开启Zero模式
#if 1
				// 启动mfglib
				emberAfMfglibStart(true);//emberAfCorePrintln("___________52");
				// 设置channel
				mfglibSetChannel(_ZERO_CHANNEL);
				mfglibSetPower(0, 20);
				emberAfCorePrint("#############[Zero] Zero opened! .......	channel: %d,  power: %d", _ZERO_CHANNEL, 20);
				__seq_enable__ = 1;
				break;
#endif
			}
			break;

		case 1:
			if(_run_ == 0){
				// 关闭Zero模式
				emberAfMfglibStop();
				emberAfCorePrint("[Zero] Zero closed! ...... Bye Bye!\n");
				__seq_enable__ = 0;
				break;
			}
			break;
		default:
			_run_ = 0;
			__seq_enable__ = 1;
			break;
	}
	

	// 判断MFG-lib运行状态
	extern bool mfgLibRunning;
	if(!mfgLibRunning){
		goto _JP_end_zeroJoinTx;
	}


	// 设备列表维护
	++_time_com;
	
	for(_deviceInfoList_t *iterm = *_devInfo_list; iterm != NULL; iterm = iterm->next){
		if(_time_com >= iterm->timeout){
			// timeout 判定处理
			if((_time_com - iterm->timeout) > _LIMIT_TIMEOUT_){
				list_remove(_devInfo_list, iterm);
			}
		}
		else{
			// timeout 判定处理
			if(((65536 - iterm->timeout) + _time_com) > _LIMIT_TIMEOUT_){
				list_remove(_devInfo_list, iterm);
			}
		}	
	}

	// 2s 事件
	if(++_time_1S >= 20){
		_time_1S = 0;
	
#if 1
		// 设备列表调试打印
		//_Zero_deviceListPrint();
#endif

		//emberAfCorePrintln("MQTT Ret: %d", _Zero_checkAndPublishMqttSimonZero());
		_Zero_checkAndPublishMqttSimonZero();
	}

	

_JP_end_zeroJoinTx:
	emberEventControlSetDelayMS(_zeroJoinTxRx_EC, 100);

}

// join 管理
// 周期 10ms
EmberEventControl _zeroJoinManage_EC;
void _zeroJoinManage_EF(void){
	emberEventControlSetInactive(_zeroJoinManage_EC);
#if 1
	// 判断MFG-lib运行状态
	extern bool mfgLibRunning;
	if(!mfgLibRunning){
		goto _JP_end_zeroJoinManage;
	}

	// 序列式发送PHY 消息
	for(_deviceInfoList_t *item = *_devInfo_list; item != NULL; item = item->next){
		if(item->sendFlag){
			if(_zero_TxSimonMessage_ControlJoin(item) == 0){
				item->sendFlag = false;
			}
			break;
		}	
	}
	

_JP_end_zeroJoinManage:
	emberEventControlSetDelayMS(_zeroJoinManage_EC, 10);
#endif
}




// PHY'payload Rx_Handler
void _zeroJoinRxPacketCallBack(uint8_t *packet,
                            uint8_t linkQuality,
                            int8_t rssi){
	(void)linkQuality;
	(void)rssi;

	// Rx 调试打印
#if 0
	emberAfCorePrint("RX: ");
	for(int i=0;i<packet[0];i++){
		emberAfCorePrint("%X", packet[i+1]);
	}
	emberAfCorePrint("\n");
#endif
#if 1

	uint8_t _eui64_info[8];
	uint8_t *_eui64_modelId;

	uint8_t packet_len = packet[0];
	uint8_t *packetPtr = packet+1;
	uint8_t *simonheaderPtr = SIMON_HEADER;
	// Simon'Header
	if(packet_len < sizeof(SIMON_HEADER))
		return;
	packet_len -= sizeof(SIMON_HEADER);


	for(uint8_t i = 0; i < sizeof(SIMON_HEADER); i ++){
		if(*(++packetPtr) != *(simonheaderPtr++))
			return;
	}
	++packetPtr;

	// Type
	if(packet_len < 1)
		return;
	packet_len -= 1;

	// Type-Info
	if(*(packetPtr++) == 0x00){
		// EUI64
		if(packet_len < 9)
			return;
		packet_len -= 9;
		if(*(packetPtr++) != 0x08){
			return;
		}
		memcpy(_eui64_info, packetPtr, 8);
		packetPtr += 8;

		// Model ID
		if(packet_len < 1)
			return;
		packet_len -= 1;
		// modelId'len
		uint8_t _len_modelId = *(packetPtr++);

		// modelId'data
		if(packet_len < _len_modelId)
			return;
		packet_len -= _len_modelId;

		_eui64_modelId = malloc(_len_modelId + 1);
		_eui64_modelId[0] = _len_modelId;
		memcpy(_eui64_modelId, packetPtr-1, _len_modelId+1);
		packetPtr += _len_modelId;

		// end
		extern void _zero_UpdateDeviceInfoList(uint8_t *eui64, uint8_t *modelId);
		_zero_UpdateDeviceInfoList(_eui64_info, _eui64_modelId);
		free(_eui64_modelId);

		// 设备列表调试打印
#if 0
		_Zero_deviceListPrint();
#endif
	}
	else{

	}
#endif
}


/********************************************************************************
*																				*
*					---- Zero 启动并设置 ---- 										*
*																				*
********************************************************************************/
int8_t _zero_StartAndSet(uint8_t channel, uint8_t csma, int8_t power){

	emberAfMfglibStart(1);

	// Channel
	if(mfglibSetChannel(channel) != 0){
		return -1;
	}

	// CSMA
	uint8_t options = csma?1:0;
	if(ezspSetValue(EZSP_VALUE_MFGLIB_OPTIONS, 1, &options) != 0){
		return -2;
	}

	// Power
	if(mfglibSetPower(EMBER_TX_POWER_MODE_DEFAULT, power) != 0){
		return -3;
	}

	return 0;
}


/********************************************************************************
*   																			*
*					---- Zero 消息发送 ----											*
*																				*
********************************************************************************/
extern uint8_t sendBuff[126];
extern EmberStatus sendPacket(uint8_t *buffer, uint16_t numPackets);
int8_t _zero_TxSimonMessage(uint8_t *message, uint8_t length){
  if(length > 125 || length < 3)
	  return -1;
  sendBuff[0] = length + 2; // message length plus 2-byte CRC
  MEMMOVE(sendBuff + 1, message, length);
  if(sendPacket(sendBuff, 1) != 0){
	  return -2;
  }

  return 0;
 
}

enum{
	_ZERO_TX_TYPE_DEVICEINFO = 0,
	_ZERO_TX_TYPE_DEVICECTL,
};





int8_t _zero_TxSimonMessage_ControlJoin(_deviceInfoList_t *item){
	uint8_t _tx_payload[128];
	uint8_t *_tx_payloadPtr = _tx_payload;

	_tx_payloadPtr++;
	// Simon Header
	memcpy(_tx_payloadPtr, SIMON_HEADER, sizeof(SIMON_HEADER));
	_tx_payloadPtr += sizeof(SIMON_HEADER);

	// Type
	*(_tx_payloadPtr++) = _ZERO_TX_TYPE_DEVICECTL;
	// EUI64
	*(_tx_payloadPtr++) = 8;	// eui64'length
	memcpy(_tx_payloadPtr, item->eui64, 8);
	_tx_payloadPtr += 8;

	// channel
	*(_tx_payloadPtr++) = __radio_channel;
	// panid
	*(_tx_payloadPtr++) = (uint8_t)(__panId & 0xFF);
	*(_tx_payloadPtr++) = (uint8_t)((__panId >> 8) & 0xFF);

	// delay
	*(_tx_payloadPtr++) = item->sendDelay;
	// nothing
	_tx_payloadPtr --;

	// TODO Total Length
	_tx_payload[0] = _tx_payloadPtr - _tx_payload;

	emberAfCorePrint("____________________1-6@@@");
	for(uint8_t i=0;i<_tx_payload[0] + 1;i++){
		
		emberAfCorePrint("%X", _tx_payload[i]);
	}
	emberAfCorePrint("@@@");
	return _zero_TxSimonMessage(_tx_payload, _tx_payload[0] + 1);
}


// Main Tick
void emberAfMainTickCallback(void){
	static bool _flag_startup = true;
	if(_flag_startup){
		_flag_startup = false;
		emberEventControlSetDelayMS(_zeroJoinTxRx_EC, 1000);
	}
}




// 更新设备列表
void _zero_UpdateDeviceInfoList(uint8_t *eui64, uint8_t *modelId){

	// 检索EUI64
	bool _flag_has_eui = false;
	for(_deviceInfoList_t *iterm = *_devInfo_list; iterm != NULL; iterm = iterm->next){
		bool _flag_eq_eui = true;
		for(uint8_t i = 0; i < 8; i ++){
			if(iterm->eui64[i] != eui64[i]){
				_flag_eq_eui = false;
				break;
			}
		}
		// 找到eui信息
		if(_flag_eq_eui){
			// 更新timeout
			iterm->timeout = _time_com;
			_flag_has_eui = true;
			break;
		}
	}

	// 没有EUI64（新设备）
	_deviceInfoList_t *iterm_add;
	if(!_flag_has_eui){
		iterm_add = (_deviceInfoList_t *)malloc(sizeof(_deviceInfoList_t));
		iterm_add->timeout = _time_com;
		memset(iterm_add->modelId, 0, 64);
		memcpy(iterm_add->modelId, modelId, modelId[0]+1);
		memset(iterm_add->eui64, 0, 8);
		memcpy(iterm_add->eui64, eui64, 8);
		iterm_add->sendFlag = false;
		iterm_add->sendDelay = 0;

		list_add(_devInfo_list, iterm_add);
		//free(iterm_add);
	}
	// 有
	else{
		return;
	}
}


// 从设备列表删除指定设备
void _zero_UpdateDeviceDelete(uint8_t *eui64, uint8_t *modelId){

}





void _Zero_deviceListPrint(void){

	static uint8_t cnt____ = 0;

	emberAfCorePrintln("------------Update Device List[%d]----------", cnt____++);
	emberAfCorePrintln("Device Count: %d", list_length(_devInfo_list));
	for(_deviceInfoList_t *iterm = *_devInfo_list; iterm != NULL; iterm = iterm->next){
		emberAfCorePrint("#DEVICE#\neui64: ");
		for(uint8_t i=0;i<8;i++){
			emberAfCorePrint("%X", iterm->eui64[i]);
		}
		emberAfCorePrint("\nmodelId: ");
		char *__model__ = malloc(iterm->modelId[0] + 1);
		memcpy(__model__, iterm->modelId+1, iterm->modelId[0]);
		__model__[iterm->modelId[0]] = '\0';

		emberAfCorePrint("%s", __model__);

		emberAfCorePrint("sendFlag: %d \n", iterm->sendFlag);

		emberAfCorePrint("sendDelay: %d \n", iterm->sendDelay);

		emberAfCorePrint("\n...\n");

		free(__model__);
	}
}


/********************************************************************************
*   																			*
*					---- Zero MQTT消息收发 ----										*
*																				*
********************************************************************************/
#include <stdlib.h>
#include <string.h>

// 取一对指定字符中间字符串
static int _QC_double_chr(char *dest, const char *dat, int chr){
	char buf[128] = {0};
	int len = 0;
	int find = 0;
	for(int i=0; i < strlen(dat); i ++){
		if(find == 1){
			if(dat[i] != chr){
				buf[len++] = dat[i];
			}
			else{
				// char *ret_str = (char *)malloc(len + 1);
				memcpy((void *)dest, buf, len);
				dest[len] = '\0';
				// return ret_str;
				return 0;
			}
		}
		else{
			if(dat[i] == chr){
				find = 1;
			}
		}
	}
	return -1;
}


// MQTT消息解析
//#include "stack-info.h"
void _Zero_handleZeroMessage(cJSON* messageJson){
#if 1
	// Zero MQTT消息调试打印
	emberAfCorePrintln("Zero Topic: \n%s", cJSON_Print(messageJson));
#endif

	

	if(cJSON_Array != messageJson->type){emberAfCorePrintln("___________1");
		// 命令
		cJSON *_js_enable_ = cJSON_GetObjectItem(messageJson, "enable");
		if(_js_enable_ != NULL){emberAfCorePrintln("___________2: %s", cJSON_Print(_js_enable_));
			char *_str_enable_ = cJSON_Print(_js_enable_);
			if(strcmp(_str_enable_, "false") == 0){emberAfCorePrintln("___________3");
				// 关闭Zero模式
				_run_ = 0;
				
			}
			else if(strcmp(_str_enable_, "true") == 0){emberAfCorePrintln("___________4");
				_run_ = 1;
			}
			else{emberAfCorePrintln("___________5");
				// TODO MQTT回复
				//////////////////////
				emberAfCorePrintln("[zero] enable error!");
			}

			free(_str_enable_);
		}
		
	
		return ;
	}

	int _size_array = cJSON_GetArraySize(messageJson);
	
	emberAfCorePrint("[zero] Size :  %d\n", _size_array);
	if(_size_array < 1){
		return ;
	}

	for(int i = 0; i < _size_array; i++){
		cJSON *_js_itemArray = cJSON_GetArrayItem(messageJson, i);//emberAfCorePrintln("___0: %s", cJSON_Print(_js_itemArray));
		if(_js_itemArray != NULL){//emberAfCorePrintln("___1");
			cJSON *_js_eui_ = cJSON_GetObjectItem(_js_itemArray, "eui");
			cJSON *_js_delay_ = cJSON_GetObjectItem(_js_itemArray, "delay");
			if(_js_eui_!= NULL && _js_delay_ != NULL){//emberAfCorePrintln("___2");
				char *_str_buf_eui_ = cJSON_Print(_js_eui_);//emberAfCorePrintln("___3:%s", _str_buf_eui_);
				char *_str_eui_ = (char *)malloc(8);
				char *_str_delay_ = cJSON_Print(_js_delay_);//emberAfCorePrintln("___4:%s", _str_delay_);
				if(_QC_double_chr(_str_eui_, _str_buf_eui_, '\"') == 0){//emberAfCorePrintln("___5:%s", _str_eui_);
					// 通过eui 遍历设备
					for(_deviceInfoList_t *item = *_devInfo_list; item != NULL; item = item->next){
						bool __eq_item__ = true;
						for(uint8_t i=0;i<8;i++){
							char __buf__[2] = {0};
							memcpy(__buf__, _str_eui_ + (i<<1), 2);
							if(item->eui64[7-i] != strtoul(__buf__, NULL, 16)){
								__eq_item__ = false;//emberAfCorePrintln("___6:eui[8-%d]:%X  toul:%X", i, item->eui64[7-i],strtoul(__buf__, NULL, 16));
								break;
							}
						}
						// 找到设备
						if(__eq_item__){//emberAfCorePrintln("___7");
							if(_str_delay_ != NULL){
								if((item->sendDelay = strtoul(_str_delay_, NULL, 10)) >= _LIMIT_MINDELAY_JOIN){
									if(item->sendDelay <= _LIMIT_MAXDELAY_JOIN)
										item->sendFlag = true;
									else{
										item->sendDelay = 0;
										emberAfCorePrint("[Zero] [eui: %s]\"Delay\" too big!\n", _str_eui_);
									}
								}
								else{
									item->sendDelay = 0;
									emberAfCorePrint("[Zero] [eui: %s]\"Delay\" too short!\n", _str_eui_);
								}
							}
							
							break;
						}
						
					}
				}

				free(_str_buf_eui_);
				free(_str_eui_);
				free(_str_delay_);
			}
		}
	}

	
#if 1
	// 调试打印
	_Zero_deviceListPrint();
#endif
	

}



#if 1
// MQTT消息发送
extern void publishMqttTopic(char * topicNameString, cJSON * nodeJson);
int8_t _Zero_checkAndPublishMqttSimonZero(void){
  	cJSON *_jsArray = cJSON_CreateArray();

	for(_deviceInfoList_t *iterm = *_devInfo_list; iterm != NULL; iterm = iterm->next){
		cJSON *itemArray = cJSON_CreateObject();
		// eui64 (转大端字符串)
		char *_eui_ = malloc(17);
		char *_euiPtr_ = _eui_;
		for(uint8_t i = 0; i < 8; i++){
			sprintf(_euiPtr_, "%X", iterm->eui64[7-i]);
			_euiPtr_ += 2;
		}
		_eui_[16] = '\0';
		cJSON_AddStringToObject(itemArray, "eui", _eui_);
		free(_eui_);
		// modelId
		char *_modelId_ = malloc(iterm->modelId[0] + 1);
		memcpy(_modelId_, iterm->modelId + 1, iterm->modelId[0]);
		_modelId_[iterm->modelId[0]] = '\0';
		cJSON_AddStringToObject(itemArray, "modelId", _modelId_);
		free(_modelId_);
		
		cJSON_AddItemToArray(_jsArray, itemArray);		
	}
  

  	publishMqttTopic(_MQTT_TOPIC_ZERO_, _jsArray);

  	return 0;
}

#endif



void _carel_GateWayGetNetworkParameter(uint8_t *channel, uint16_t *panId){
	EmberNetworkParameters networkParams = { 0 };
	EmberNodeType nodeType;
	emberAfGetNetworkParameters(&nodeType, &networkParams);
	(void)nodeType;
	*channel = networkParams.radioChannel;
	*panId = networkParams.panId;
}


