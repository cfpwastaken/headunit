#pragma once
#include "hu_uti.h"
#include "hu.pb.h"
#include "hu_ssl.h"
#include <functional>
#include <thread>

// Channels ( or Service IDs)
#define AA_CH_CTR 0                                                                                  // Sync with hu_tra.java, hu_aap.h and hu_aap.c:aa_type_array[]
#define AA_CH_TOU 1
#define AA_CH_SEN 2
#define AA_CH_VID 3
#define AA_CH_AUD 4
#define AA_CH_AU1 5
#define AA_CH_AU2 6
#define AA_CH_MIC 7
#define AA_CH_BT  8
#define AA_CH_MAX 256

enum HU_STATE
{
  hu_STATE_INITIAL = 0,
  hu_STATE_STARTIN  = 1,
  hu_STATE_STARTED = 2,
  hu_STATE_STOPPIN = 3,
  hu_STATE_STOPPED = 4,
};

const char* state_get(int s);

enum HU_FRAME_FLAGS
{
  HU_FRAME_FIRST_FRAME = 1 << 0,
  HU_FRAME_LAST_FRAME = 1 << 1,
  HU_FRAME_CONTROL_MESSAGE = 1 << 2,
  HU_FRAME_ENCRYPTED = 1 << 3,
};

#define MAX_FRAME_PAYLOAD_SIZE 0x4000
//At 16 bytes for header
#define MAX_FRAME_SIZE 0x4100

class HUTransportStream
{
protected:
  int readfd = -1;
  //optional if required for pipe, etc
  int errorfd = -1;
public:
  virtual ~HUTransportStream() {}
  inline HUTransportStream() {}
  virtual int Start(byte ep_in_addr, byte ep_out_addr) = 0;
  virtual int Stop() = 0;
  virtual int Write(const byte* buf, int len, int tmo) = 0;

  inline int GetReadFD() { return readfd; }  
  inline int GetErrorFD() { return errorfd; } 
};

class IHUConnectionThreadInterface;

class IHUAnyThreadInterface
{
protected:
  ~IHUAnyThreadInterface() {}
  IHUAnyThreadInterface() {}
public:
  typedef std::function<void(IHUConnectionThreadInterface&)> HUThreadCommand;
  //Can be called from any thread
  virtual int hu_queue_command(HUThreadCommand&& command) = 0;
};

class IHUConnectionThreadInterface : public IHUAnyThreadInterface
{
protected:
  ~IHUConnectionThreadInterface() {}
  IHUConnectionThreadInterface() {}
public:
  virtual int hu_aap_enc_send_message(int retry, int chan, uint16_t messageCode, const google::protobuf::MessageLite& message, int overrideTimeout = -1) = 0;
  virtual int hu_aap_enc_send_media_packet(int retry, int chan, uint16_t messageCode, uint64_t timeStamp, const byte* buffer, int bufferLen, int overrideTimeout = -1) = 0;
  virtual int hu_aap_unenc_send_blob(int retry, int chan, uint16_t messageCode, const byte* buffer, int bufferLen, int overrideTimeout = -1) = 0;
  virtual int hu_aap_unenc_send_message(int retry, int chan, uint16_t messageCode, const google::protobuf::MessageLite& message, int overrideTimeout = -1) = 0;

  template<typename EnumType>
  inline int hu_aap_enc_send_message(int retry, int chan, EnumType messageCode, const google::protobuf::MessageLite& message, int overrideTimeout = -1)
  {
    return hu_aap_enc_send_message(retry, chan, static_cast<uint16_t>(messageCode), message, overrideTimeout);
  }

  template<typename EnumType>
  inline int hu_aap_enc_send_media_packet(int retry, int chan, EnumType messageCode, uint64_t timeStamp, const byte* buffer, int bufferLen, int overrideTimeout = -1)
  {
    return hu_aap_enc_send_media_packet(retry, chan, static_cast<uint16_t>(messageCode), timeStamp, buffer, bufferLen, overrideTimeout);
  }

  template<typename EnumType>
  inline int hu_aap_unenc_send_blob(int retry, int chan, EnumType messageCode, const byte* buffer, int bufferLen, int overrideTimeout = -1)
  {
    return hu_aap_unenc_send_blob(retry, chan, static_cast<uint16_t>(messageCode), buffer, bufferLen, overrideTimeout);
  }

  template<typename EnumType>
  inline int hu_aap_unenc_send_message(int retry, int chan, EnumType messageCode, const google::protobuf::MessageLite& message, int overrideTimeout = -1)
  {
    return hu_aap_unenc_send_message(retry, chan, static_cast<uint16_t>(messageCode), message, overrideTimeout);
  }

  virtual int hu_aap_stop() = 0;  
};

//These callbacks are executed in the HU thread
class IHUConnectionThreadEventCallbacks
{
protected:
  ~IHUConnectionThreadEventCallbacks() {}
  IHUConnectionThreadEventCallbacks() {}
public:

  //return > 0 if handled < 0 for error
  virtual int MessageFilter(IHUConnectionThreadInterface& stream, HU_STATE state,  int chan, uint16_t msg_type, const byte * buf, int len) { return 0; }

  //return -1 for error
  virtual int MediaPacket(int chan, uint64_t timestamp, const byte * buf, int len) = 0;
  virtual int MediaStart(int chan) = 0;
  virtual int MediaStop(int chan) = 0;

  virtual void DisconnectionOrError() = 0;

  virtual void CustomizeCarInfo(HU::ServiceDiscoveryResponse& carInfo) {}
  virtual void CustomizeInputConfig(HU::ChannelDescriptor::InputEventChannel& inputChannel) {}
  virtual void CustomizeSensorConfig(HU::ChannelDescriptor::SensorChannel& sensorChannel) {}
  virtual void CustomizeOutputChannel(int chan, HU::ChannelDescriptor::OutputStreamChannel& streamChannel) {}
  virtual void CustomizeInputChannel(int chan, HU::ChannelDescriptor::InputStreamChannel& streamChannel) {}
  virtual void CustomizeBluetoothService(int chan, HU::ChannelDescriptor::BluetoothService& bluetoothService) {}
};


class HUServer : protected IHUConnectionThreadInterface
{
public:
  //Must be called from the "main" thread (as defined by the user)
  int hu_aap_start    (byte ep_in_addr, byte ep_out_addr);                 // Used by          hu_mai,  hu_jni     // Starts USB/ACC/OAP, then AA protocol w/ VersReq(1), SSL handshake, Auth Complete
  int hu_aap_shutdown ();

  HUServer(IHUConnectionThreadEventCallbacks& callbacks);
  ~HUServer() { hu_aap_shutdown(); }

  inline IHUAnyThreadInterface& GetAnyThreadInterface() { return *this; }

protected:
  IHUConnectionThreadEventCallbacks& callbacks;
  std::unique_ptr<HUTransportStream> transport;
  HU_STATE iaap_state = hu_STATE_INITIAL;
  int iaap_tra_recv_tmo = 150;//100;//1;//10;//100;//250;//100;//250;//100;//25; // 10 doesn't work ? 100 does
  int iaap_tra_send_tmo = 500;//2;//25;//250;//500;//100;//500;//250;
  std::vector<uint8_t> temp_assembly_buffer;
  byte enc_buf[MAX_FRAME_SIZE] = {0};
  int32_t channel_session_id[AA_CH_MAX] = {0};

  std::thread hu_thread;
  int command_read_fd = -1;
  int command_write_fd = -1;
  bool hu_thread_quit_flag = false;

  HUThreadCommand* hu_pop_command();

  void hu_thread_main();

  SSL_METHOD  * hu_ssl_method  = NULL;
  SSL_CTX     * hu_ssl_ctx     = NULL;
  SSL         * hu_ssl_ssl    = NULL;
  BIO         * hu_ssl_rm_bio = NULL;
  BIO         * hu_ssl_wm_bio = NULL;

  void hu_ssl_ret_log (int ret);
  void hu_ssl_inf_log();

  int send_ssl_handshake_packet();
  int hu_ssl_begin_handshake ();
  int hu_handle_SSLHandshake(int chan, byte * buf, int len);

  int ihu_tra_start (byte ep_in_addr, byte ep_out_addr);
  int ihu_tra_stop();
  int iaap_msg_process (int chan, uint16_t msg_type, byte * buf, int len);

  int hu_aap_tra_recv (byte * buf, int len, int tmo);                      // Used by intern,                      hu_ssl
  int hu_aap_tra_send (int retry, byte * buf, int len, int tmo);                      // Used by intern,                      hu_ssl
  int hu_aap_enc_send (int retry, int chan, byte * buf, int len, int overrideTimeout = -1);                     // Used by intern,            hu_jni     // Encrypted Send
  int hu_aap_unenc_send (int retry, int chan, byte * buf, int len, int overrideTimeout = -1); 

  int hu_aap_recv_process (int tmo);                                              // Used by          hu_mai,  hu_jni     // Process 1 encrypted receive message set:
                                                                                                                          // Respond to decrypted message
  virtual int hu_aap_enc_send_message(int retry, int chan, uint16_t messageCode, const google::protobuf::MessageLite& message, int overrideTimeout = -1) override;
  virtual int hu_aap_enc_send_media_packet(int retry, int chan, uint16_t messageCode, uint64_t timeStamp, const byte* buffer, int bufferLen, int overrideTimeout = -1) override;
  virtual int hu_aap_unenc_send_blob(int retry, int chan, uint16_t messageCode, const byte* buffer, int bufferLen, int overrideTimeout = -1) override;
  virtual int hu_aap_unenc_send_message(int retry, int chan, uint16_t messageCode, const google::protobuf::MessageLite& message, int overrideTimeout = -1) override;
  virtual int hu_aap_stop     () override;    

  using IHUConnectionThreadInterface::hu_aap_enc_send_message;
  using IHUConnectionThreadInterface::hu_aap_enc_send_media_packet;
  using IHUConnectionThreadInterface::hu_aap_unenc_send_blob;
  using IHUConnectionThreadInterface::hu_aap_unenc_send_message;
 
  int hu_handle_VersionResponse (int chan, byte * buf, int len);
  int hu_handle_ServiceDiscoveryRequest (int chan, byte * buf, int len);
  int hu_handle_PingRequest (int chan, byte * buf, int len);
  int hu_handle_NavigationFocusRequest (int chan, byte * buf, int len);
  int hu_handle_ShutdownRequest (int chan, byte * buf, int len);
  int hu_handle_VoiceSessionRequest (int chan, byte * buf, int len);
  int hu_handle_AudioFocusRequest (int chan, byte * buf, int len);
  int hu_handle_ChannelOpenRequest(int chan, byte * buf, int len);
  int hu_handle_MediaSetupRequest(int chan, byte * buf, int len);
  int hu_handle_VideoFocusRequest(int chan, byte * buf, int len);
  int hu_handle_MediaStartRequest(int chan, byte * buf, int len);
  int hu_handle_MediaStopRequest(int chan, byte * buf, int len);
  int hu_handle_SensorStartRequest (int chan, byte * buf, int len);
  int hu_handle_BindingRequest (int chan, byte * buf, int len);
  int hu_handle_MediaAck (int chan, byte * buf, int len);
  int hu_handle_MicRequest (int chan, byte * buf, int len);
  int hu_handle_MediaDataWithTimestamp (int chan, byte * buf, int len);
  int hu_handle_MediaData(int chan, byte * buf, int len);

    //Can be called from any thread
  virtual int hu_queue_command(IHUAnyThreadInterface::HUThreadCommand&& command) override;
};

enum class HU_INIT_MESSAGE : uint16_t
{
  VersionRequest = 0x0001,
  VersionResponse = 0x0002,
  SSLHandshake = 0x0003,
  AuthComplete = 0x0004,
};

enum class HU_PROTOCOL_MESSAGE : uint16_t
{
  MediaDataWithTimestamp = 0x0000,
  MediaData = 0x0001,
  ServiceDiscoveryRequest = 0x0005,
  ServiceDiscoveryResponse = 0x0006,    
  ChannelOpenRequest = 0x0007,
  ChannelOpenResponse = 0x0008,
  PingRequest = 0x000b,
  PingResponse = 0x000c,
  NavigationFocusRequest = 0x000d,
  NavigationFocusResponse = 0x000e,
  ShutdownRequest = 0x000f,
  ShutdownResponse = 0x0010,
  VoiceSessionRequest = 0x0011,
  AudioFocusRequest = 0x0012,
  AudioFocusResponse = 0x0013,
};                                                                                                                           // If video data, put on queue

enum class HU_MEDIA_CHANNEL_MESSAGE : uint16_t
{
  MediaSetupRequest = 0x8000, //Setup
  MediaStartRequest = 0x8001, //Start
  MediaStopRequest = 0x8002, //Stop
  MediaSetupResponse = 0x8003, //Config
  MediaAck = 0x8004,
  MicRequest = 0x8005,
  MicReponse = 0x8006,
  VideoFocusRequest = 0x8007,
  VideoFocus = 0x8008,
};    

enum class HU_SENSOR_CHANNEL_MESSAGE : uint16_t
{
  SensorStartRequest = 0x8001,
  SensorStartResponse = 0x8002,
  SensorEvent = 0x8003,
};    

enum class HU_INPUT_CHANNEL_MESSAGE : uint16_t
{
  InputEvent = 0x8001,
  BindingRequest = 0x8002,
  BindingResponse = 0x8003,
};    

enum HU_INPUT_BUTTON
{
    HUIB_UP = 0x13,
    HUIB_DOWN = 0x14,
    HUIB_LEFT = 0x15,
    HUIB_RIGHT = 0x16,
    HUIB_BACK = 0x04,
    HUIB_ENTER = 0x17,
    HUIB_MIC = 0x54,
    HUIB_PLAYPAUSE = 0x55,
    HUIB_NEXT = 0x57,
    HUIB_PREV = 0x58,
    HUIB_PHONE = 0x5,
    HUIB_START = 126,
    HUIB_STOP = 127,
    HUIB_SCROLLWHEEL = 65536,
    
};

