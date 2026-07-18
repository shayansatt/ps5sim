#ifndef EMULATOR_INCLUDE_EMULATOR_NETWORK_H_
#define EMULATOR_INCLUDE_EMULATOR_NETWORK_H_

#include "common/abi.h"
#include "common/common.h"
#include "common/subsystems.h"

namespace Libs::Network {

PS5SIM_SUBSYSTEM_DEFINE(Network);

namespace Net {

struct NetEtherAddr;

union NetEpollData {
	void*    ptr;
	uint32_t u32;
	int      fd;
	uint64_t u64;
};

struct NetEpollEvent {
	uint32_t     events;
	uint32_t     reserved;
	uint64_t     ident;
	NetEpollData data;
};

static_assert(sizeof(NetEpollData) == 8);
static_assert(sizeof(NetEpollEvent) == 24);

int PS5SIM_SYSV_ABI NetInit();
int PS5SIM_SYSV_ABI NetTerm();
int PS5SIM_SYSV_ABI NetPoolCreate(const char* name, int size, int flags);
int PS5SIM_SYSV_ABI NetPoolDestroy(int memid);
int PS5SIM_SYSV_ABI NetResolverCreate(const char* name, int memid, int flags);
int PS5SIM_SYSV_ABI NetResolverStartNtoa(int rid, const char* hostname, void* addr, int timeout,
                                       int retry, int flags);
int PS5SIM_SYSV_ABI NetInetPton(int af, const char* src, void* dst);
const char* PS5SIM_SYSV_ABI NetInetNtop(int af, const void* src, char* dst, uint32_t size);
int PS5SIM_SYSV_ABI         NetEtherNtostr(const NetEtherAddr* n, char* str, size_t len);
int PS5SIM_SYSV_ABI         NetGetMacAddress(NetEtherAddr* addr, int flags);
int PS5SIM_SYSV_ABI         NetGetSockInfo(int s, void* info, int n, int flags);
int PS5SIM_SYSV_ABI         EpollCreate(const char* name, int flags);
int PS5SIM_SYSV_ABI         EpollControl(int eid, int op, int id, const NetEpollEvent* event);
int PS5SIM_SYSV_ABI         EpollWait(int eid, NetEpollEvent* events, int maxevents, int timeout);
int PS5SIM_SYSV_ABI         EpollDestroy(int eid);
bool PS5SIM_SYSV_ABI        IsSocket(int s);
int PS5SIM_SYSV_ABI         SocketClose(int s);
int PS5SIM_SYSV_ABI         Socket(int family, int type, int protocol);
int PS5SIM_SYSV_ABI         Bind(int s, const void* addr, uint32_t addrlen);
int PS5SIM_SYSV_ABI         Connect(int s, const void* addr, uint32_t addrlen);
int PS5SIM_SYSV_ABI         Listen(int s, int backlog);
int PS5SIM_SYSV_ABI         Accept(int s, void* addr, uint32_t* addrlen);
int PS5SIM_SYSV_ABI         Shutdown(int s, int how);
int PS5SIM_SYSV_ABI         Getsockname(int s, void* addr, uint32_t* addrlen);
int PS5SIM_SYSV_ABI         Getsockopt(int s, int level, int optname, void* optval, uint32_t* optlen);
int PS5SIM_SYSV_ABI Setsockopt(int s, int level, int optname, const void* optval, uint32_t optlen);
int PS5SIM_SYSV_ABI Select(int nfds, void* readfds, void* writefds, void* exceptfds,
                         const void* timeout);
int64_t PS5SIM_SYSV_ABI Send(int s, const void* buf, uint64_t len, int flags);
int64_t PS5SIM_SYSV_ABI Recv(int s, void* buf, uint64_t len, int flags);

} // namespace Net

namespace Ssl {

int PS5SIM_SYSV_ABI SslInit(uint64_t pool_size);
int PS5SIM_SYSV_ABI SslTerm(int ssl_ctx_id);
int PS5SIM_SYSV_ABI SslGetCaCerts(int ssl_ctx_id, void* ca_certs);
int PS5SIM_SYSV_ABI SslFreeCaCerts(int ssl_ctx_id, void* ca_certs);

} // namespace Ssl

namespace Http {

struct HttpEpoll;
struct HttpNBEvent;

using HttpEpollHandle = HttpEpoll*;

using HttpsCallback = PS5SIM_SYSV_ABI int (*)(int, unsigned int, void* const*, int, void*);

int PS5SIM_SYSV_ABI HttpInit(int memid, int ssl_ctx_id, uint64_t pool_size);
int PS5SIM_SYSV_ABI HttpTerm(int http_ctx_id);
int PS5SIM_SYSV_ABI HttpCreateTemplate(int http_ctx_id, const char* user_agent, int http_ver,
                                     int is_auto_proxy_conf);
int PS5SIM_SYSV_ABI HttpDeleteTemplate(int tmpl_id);
int PS5SIM_SYSV_ABI HttpSetNonblock(int id, int enable);
int PS5SIM_SYSV_ABI HttpCreateEpoll(int http_ctx_id, HttpEpollHandle* eh);
int PS5SIM_SYSV_ABI HttpDestroyEpoll(int http_ctx_id, HttpEpollHandle eh);
int PS5SIM_SYSV_ABI HttpCreateConnection(int tmpl_id, const char* server_name, const char* scheme,
                                       uint16_t port, int enable_keep_alive);
int PS5SIM_SYSV_ABI HttpCreateConnectionWithURL(int tmpl_id, const char* url, int enable_keep_alive);
int PS5SIM_SYSV_ABI HttpDeleteConnection(int conn_id);
int PS5SIM_SYSV_ABI HttpCreateRequest(int conn_id, int method, const char* path,
                                    uint64_t content_length);
int PS5SIM_SYSV_ABI HttpCreateRequestWithURL2(int conn_id, const char* method, const char* url,
                                            uint64_t content_length);
int PS5SIM_SYSV_ABI HttpSetRequestContentLength(int request_id, uint64_t content_length);
int PS5SIM_SYSV_ABI HttpDeleteRequest(int req_id);
int PS5SIM_SYSV_ABI HttpAddRequestHeader(int id, const char* name, const char* value, uint32_t mode);
int PS5SIM_SYSV_ABI HttpSetEpoll(int id, HttpEpollHandle eh, void* user_arg);
int PS5SIM_SYSV_ABI HttpUnsetEpoll(int id);
int PS5SIM_SYSV_ABI HttpSendRequest(int request_id, const void* post_data, size_t size);
int PS5SIM_SYSV_ABI HttpAbortRequest(int request_id);
int PS5SIM_SYSV_ABI HttpWaitRequest(HttpEpollHandle eh, HttpNBEvent* nbev, int maxevents,
                                  int timeout);
int PS5SIM_SYSV_ABI HttpGetStatusCode(int request_id, int* status_code);
int PS5SIM_SYSV_ABI HttpGetAllResponseHeaders(int request_id, char** header, size_t* header_size);
int PS5SIM_SYSV_ABI HttpGetResponseContentLength(int request_id, int* result,
                                               uint64_t* content_length);
int PS5SIM_SYSV_ABI HttpsSetSslCallback(int id, HttpsCallback cbfunc, void* user_arg);
int PS5SIM_SYSV_ABI HttpsSetMinSslVersion(int id, uint32_t ssl_version);
int PS5SIM_SYSV_ABI HttpsDisableOption(int id, uint32_t ssl_flags);
int PS5SIM_SYSV_ABI HttpSetResolveTimeOut(int id, uint32_t usec);
int PS5SIM_SYSV_ABI HttpSetResolveRetry(int id, int32_t retry);
int PS5SIM_SYSV_ABI HttpSetConnectTimeOut(int id, uint32_t usec);
int PS5SIM_SYSV_ABI HttpSetSendTimeOut(int id, uint32_t usec);
int PS5SIM_SYSV_ABI HttpSetRecvTimeOut(int id, uint32_t usec);
int PS5SIM_SYSV_ABI HttpSetAutoRedirect(int id, int enable);
int PS5SIM_SYSV_ABI HttpSetAuthEnabled(int id, int enable);

} // namespace Http

namespace NetCtl {

struct NetCtlNatInfo;
union NetCtlInfo;

using NetCtlCallback = PS5SIM_SYSV_ABI void (*)(int, void*);

int PS5SIM_SYSV_ABI  NetCtlInit();
void PS5SIM_SYSV_ABI NetCtlTerm();
int PS5SIM_SYSV_ABI  NetCtlGetNatInfo(NetCtlNatInfo* nat_info);
int PS5SIM_SYSV_ABI  NetCtlCheckCallback();
int PS5SIM_SYSV_ABI  NetCtlGetState(int* state);
int PS5SIM_SYSV_ABI  NetCtlGetStateV6(int* state);
int PS5SIM_SYSV_ABI  NetCtlRegisterCallback(NetCtlCallback func, void* arg, int* cid);
int PS5SIM_SYSV_ABI  NetCtlUnregisterCallback(int cid);
int PS5SIM_SYSV_ABI  NetCtlGetResult(int event_type, int* error_code);
int PS5SIM_SYSV_ABI  NetCtlGetInfo(int code, NetCtlInfo* info);

} // namespace NetCtl

namespace NpManager {

struct NpTitleId;
struct NpTitleSecret;
struct NpContentRestriction;
struct NpId;
struct NpOnlineId;
struct NpCreateAsyncRequestParameter;
struct NpCheckPremiumParameter;
struct NpCheckPremiumResult;

int PS5SIM_SYSV_ABI  NpCheckCallback();
int PS5SIM_SYSV_ABI  NpSetNpTitleId(const NpTitleId* title_id, const NpTitleSecret* title_secret);
int PS5SIM_SYSV_ABI  NpSetContentRestriction(const NpContentRestriction* restriction);
int PS5SIM_SYSV_ABI  NpRegisterStateCallback(void* callback, void* userdata);
int PS5SIM_SYSV_ABI  NpUnregisterStateCallback();
void PS5SIM_SYSV_ABI NpRegisterGamePresenceCallback(void* callback, void* userdata);
int PS5SIM_SYSV_ABI  NpRegisterPlusEventCallback(void* callback, void* userdata);
int PS5SIM_SYSV_ABI  NpRegisterPremiumEventCallback(void* callback, void* userdata);
int PS5SIM_SYSV_ABI  NpRegisterNpReachabilityStateCallback(void* callback, void* userdata);
int PS5SIM_SYSV_ABI  NpGetNpId(int user_id, NpId* np_id);
int PS5SIM_SYSV_ABI  NpGetOnlineId(int user_id, NpOnlineId* online_id);
int PS5SIM_SYSV_ABI  NpGetAccountIdA(int user_id, uint64_t* account_id);
int PS5SIM_SYSV_ABI  NpGetAccountCountryA(int user_id, void* country_code);
int PS5SIM_SYSV_ABI  NpGetAccountAge(int req_id, int user_id, uint8_t* age);
int PS5SIM_SYSV_ABI  NpCreateRequest();
int PS5SIM_SYSV_ABI  NpCreateAsyncRequest(const NpCreateAsyncRequestParameter* param);
int PS5SIM_SYSV_ABI  NpDeleteRequest(int req_id);
int PS5SIM_SYSV_ABI  NpAbortRequest(int req_id);
int PS5SIM_SYSV_ABI  NpCheckNpAvailability(int req_id, const char* user, void* result);
int PS5SIM_SYSV_ABI  NpCheckNpReachability(int req_id, int user_id);
int PS5SIM_SYSV_ABI  NpPollAsync(int req_id, int* result);
int PS5SIM_SYSV_ABI  NpCheckPremium(int req_id, const NpCheckPremiumParameter* param,
                                  NpCheckPremiumResult* result);
int PS5SIM_SYSV_ABI  NpGetState(int user_id, uint32_t* state);
int PS5SIM_SYSV_ABI  NpGetNpReachabilityState(int user_id, uint32_t* state);

} // namespace NpManager

} // namespace Libs::Network

#endif /* EMULATOR_INCLUDE_EMULATOR_NETWORK_H_ */
