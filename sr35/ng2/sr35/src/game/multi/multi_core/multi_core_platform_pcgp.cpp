#if defined(__PC_GP)

#include "interface/main_menu.h"
#include "multi_core_platform_internal.h"
#include "multi_core_platform_pcgp.h"
#include "vlib/memory/v_mempool_base.h"
#include "game/interface/localization/localization.h"
#include "multi/multi_game/multi_game_error.h"

#include <Xal/xal.h>
#include <xsapi-c/services_c.h>
#include <XGame.h>
#include <XStore.h>
#include <XGameUI.h>

#include "game/gamewide/stats.h"

// --------------------
//
// Defines
//
// --------------------

#define LIVE_POOL_DATA_SIZE					(4 * 1024 * 1024)
#define LIVE_POOL_PAGE_SIZE					(32)

#define LIVE_GAME_SERVICE_CONFIG_ID			L"097d0100-e05c-4d37-8420-46f1169056cf"
#define LIVE_GAME_SESSION_TEMPLATE_NAME		"GameSession"
#define LIVE_LOBBY_SESSION_TEMPLATE_NAME	"LobbySession"

#define MATCHING_TIMEOUT_IN_SECONDS			(60*60)
#define LIVE_DEFAULT_HOPPER_NAME			"Normal"

// --------------------
//
// Helper Macros
//
// --------------------


// --------------------
//
// Enumerated Types
//
// --------------------

enum live_login_state {
	LIVE_LOGIN_STATE_IDLE,
	LIVE_LOGIN_STATE_LOGIN_SILENT,
	LIVE_LOGIN_STATE_LOGIN_WITH_UI,
	LIVE_LOGIN_STATE_RESOLVE_USER_ISSUE,
	LIVE_LOGIN_STATE_HANDLE_LOGIN_RESULT,
	LIVE_LOGIN_STATE_CONNECTED,
	LIVE_LOGIN_STATE_LOGOUT,
};

enum live_auto_matchmaking_state
{
	LIVE_AUTO_MATCHMAKING_IDLE,
	LIVE_AUTO_MATCHMAKING_ACQUIRE_SESSION,
	LIVE_AUTO_MATCHMAKING_CONNECTED,
	LIVE_AUTO_MATCHMAKING_FAILED,
};


// --------------------
//
// Structures
//
// --------------------
struct LiveSessionData {
	size_t		members_count;
	bool		is_auto_match;
	uint64_t	host_xuid;
	uint64_t	host_raknet_guid;
	uint64_t	client_raknet_guid;
	GUID		session_guid;
	bool		is_host;

	LiveSessionData() {
		reset();
	}

	void reset() {
		members_count = 0;
		is_auto_match = false;
		host_xuid = 0;
		host_raknet_guid = 0;
		client_raknet_guid = 0;
		session_guid = GUID();
		is_host = false;
	}
};


// --------------------
//
// Classes
//
// --------------------


// --------------------
//
// Global Variables
//
// --------------------

XTaskQueueHandle Live_async_queue;
XTaskQueueRegistrationToken Live_user_changed_event_token;
XblContextHandle Live_context;
const XblMultiplayerEvent* Live_multiplayer_events;
size_t Live_multiplayer_events_count;


// --------------------
//
// Local Variables
//
// --------------------
static multi_core_platform_join_info Join_info;

static bool Xbox_live_inited = false;
static bool Live_UI_active = false;

static char Live_mempool_buffer[LIVE_POOL_DATA_SIZE];
static page_mempool Live_mempool(Live_mempool_buffer, LIVE_POOL_DATA_SIZE, LIVE_POOL_PAGE_SIZE, "live data pool", 16);
static mutex Live_mem_pool_mutex;

static multi_core_platform_login_result Live_login_result = LOGIN_RESULT_FAILED_GENERIC;
static multi_core_platform_signin_cb Signin_cb = NULL;
static uint Login_spinner_h = INVALID_DIALOG_BOX_HANDLE;

static live_login_state Login_state = LIVE_LOGIN_STATE_IDLE;
static XUserHandle XUser_handle = nullptr;

static multi_core_platform_pcgp_auto_matchmaking_cb Auto_automatchmake_cb = NULL;
static live_auto_matchmaking_state Auto_matchmaking_state = LIVE_AUTO_MATCHMAKING_IDLE;
static LiveSessionData Live_session_data;

// --------------------
//
// Console Functions 
//
// --------------------


// --------------------
//
// Internal Functions
//
// --------------------

static void* live_alloc_function(size_t size, HCMemoryType memoryType)
{
	mutex_lock ml(Live_mem_pool_mutex, true);
	void* data = V_ALLOC_ALIGN(Live_mempool, size, MP_DEFAULT_ALIGNMENT);
	V_ASSERT_RELEASE(data != NULL);
	return data;
}

static void live_free_function(void *pointer, HCMemoryType memoryType)
{
	mutex_lock ml(Live_mem_pool_mutex, true);
	V_FREE(Live_mempool, pointer);
}

template <class T> static T* Live_mempool_new()
{
	T* instance = (T*)V_ALLOC(Live_mempool, sizeof(T));
	return new (instance) T();
}

template <class T> static void Live_mempool_delete(T* instance)
{
	instance->~T();
	V_FREE(Live_mempool, instance);
}

XAsyncBlock* live_create_new_async_block()
{
	return Live_mempool_new<XAsyncBlock>();
}

void live_delete_async_block(XAsyncBlock* asyncBlock)
{
	Live_mempool_delete(asyncBlock);
}

static void live_show_native_error_dialog(HRESULT error, const char* context) {
	XAsyncBlock* async = Live_mempool_new<XAsyncBlock>();
	async->queue = Live_async_queue;
	async->callback = [](XAsyncBlock* async) {
		XGameUiShowErrorDialogResult(async);
		Live_mempool_delete(async);
	};

	XGameUiShowErrorDialogAsync(async, error, context);
}

void live_login_with_ui();
static void live_handle_login_failure(HRESULT error)
{
	if (error == E_XAL_UIREQUIRED) {
		Login_state = LIVE_LOGIN_STATE_IDLE;
		live_login_with_ui();
		return;
	}

	Live_login_result = LOGIN_RESULT_FAILED_GENERIC;
	Login_state = LIVE_LOGIN_STATE_HANDLE_LOGIN_RESULT;
}

static void live_handle_login_result(multi_core_platform_login_result result)
{
	if (result == LOGIN_RESULT_SUCCESS) {
		//Try_reconnect = false;
		Login_state = LIVE_LOGIN_STATE_CONNECTED;
	}
	else {
		Login_state = LIVE_LOGIN_STATE_IDLE;
	}

	if (Login_spinner_h != INVALID_DIALOG_BOX_HANDLE) {
		dialog_box *dbp = dialog_box::find(Login_spinner_h);
		V_ASSERT(dbp != NULL);
		if (dbp) {
			dbp->force_close();
		}
		Login_spinner_h = INVALID_DIALOG_BOX_HANDLE;
	}

	if (Signin_cb != NULL) {
		Signin_cb(result);
		Signin_cb = NULL;
	}
	else if (result == LOGIN_RESULT_SUCCESS) {
		multi_core_platform_login_changed(true);
	}
}

void live_set_current_user(XUserHandle, uint64_t);
void live_login_resolve_issue_with_ui(XUserHandle);
void CALLBACK live_add_user_callback(XAsyncBlock* asyncBlock) {
	XUserHandle user = nullptr;
	HRESULT hr = XUserAddResult(asyncBlock, &user);
	if (SUCCEEDED(hr)) {
		uint64_t xuid = 0;
		hr = XUserGetId(user, &xuid);
#if defined(FAKE_USER_ENABLED)
		if (FAILED(hr)) {
			xuid = 12345678;
			My_player_uid.set(xuid);
		}
		if (true) {
#else
		if (SUCCEEDED(hr)) {
#endif
			live_set_current_user(user, xuid);

			Live_login_result = LOGIN_RESULT_SUCCESS;
			Login_state = LIVE_LOGIN_STATE_HANDLE_LOGIN_RESULT;
		}
		else if (Login_state == LIVE_LOGIN_STATE_LOGIN_WITH_UI) {
			XUserCloseHandle(user);
			// Try to resolve the issue with UI
			live_login_resolve_issue_with_ui(user);
		}
		else {
			XUserCloseHandle(user);
			live_handle_login_failure(hr);
		}
	}
	else {
		live_handle_login_failure(hr);
	}

	Live_mempool_delete(asyncBlock);
}

void live_login_with_ui()
{
	My_player_uid.invalidate();
	XUser_handle = nullptr;

	XAsyncBlock* async = Live_mempool_new<XAsyncBlock>();
	async->queue = Live_async_queue;
	async->callback = live_add_user_callback;

	HRESULT hr = XUserAddAsync(XUserAddOptions::AllowGuests, async);
	if (FAILED(hr)) {
		live_handle_login_failure(hr);
	}
	Login_state = LIVE_LOGIN_STATE_LOGIN_WITH_UI;
}

void live_login_resolve_issue_with_ui(XUserHandle user) {
	XAsyncBlock* resolve_async = Live_mempool_new<XAsyncBlock>();
	resolve_async->queue = Live_async_queue;
	resolve_async->callback = [](XAsyncBlock* resolve_async) {
		HRESULT hr = XUserResolveIssueWithUiResult(resolve_async);
		if (SUCCEEDED(hr)) {
			// User solved the issue, try to re-login
			live_login_with_ui();
		}
		else {
			live_handle_login_failure(hr);
		}
		Live_mempool_delete(resolve_async);
	};
	HRESULT hr = XUserResolveIssueWithUiAsync(user, "https://www.xboxlive.com", resolve_async);
	if (FAILED(hr)) {
		live_handle_login_failure(hr);
	}
	Login_state = LIVE_LOGIN_STATE_RESOLVE_USER_ISSUE;
}

void live_login_silent()
{
	My_player_uid.invalidate();
	XUser_handle = nullptr;

	XAsyncBlock* async = Live_mempool_new<XAsyncBlock>();
	async->queue = Live_async_queue;
	async->callback = live_add_user_callback;

	HRESULT hr = XUserAddAsync(XUserAddOptions::AddDefaultUserSilently, async);
	if (FAILED(hr)) {
		live_handle_login_failure(hr);
	}
	Login_state = LIVE_LOGIN_STATE_LOGIN_SILENT;
}

void live_start_login(bool silent_login = true)
{
	if (Login_state != LIVE_LOGIN_STATE_IDLE) {
		// alrady running
		return;
	}

	if (Login_spinner_h == INVALID_DIALOG_BOX_HANDLE) {
		dialog_box* dbp = dialog_box::create(LOCALIZE("COMMUNITY_LOGGING_IN"), DIALOG_PRIORITY_GAME_CRITICAL);
		V_ASSERT_RETURN(dbp != NULL);
		dbp->add_spinner(LOCALIZE("MM_LOAD_LOADING"));
		Login_spinner_h = dbp->get_handle();
	}

	live_login_silent();
}

void CALLBACK live_user_changed_callback(void* context, XUserLocalId userLocalId, XUserChangeEvent event)
{
	XUserHandle user = nullptr;
	HRESULT hr = XUserFindUserByLocalId(userLocalId, &user);
	if (FAILED(hr)) {
		debug_printf("multi", "Failed to find user by local id.\n");
		return;
	}

	switch (event)
	{
		break;
	case XUserChangeEvent::SignedOut:
		if (!XUserCompare(user, XUser_handle)) {
			live_set_current_user(nullptr, 0);
			multi_core_platform_login_changed(true);
		}		
		break;
	default:
		break;
	}
}

static void auto_matchmaking_search_cancel(dialog_box *_dbp, Dialog_results _result)
{
	if (_result == DIALOG_RESULT_OPTION_1) {
		Login_spinner_h = INVALID_DIALOG_BOX_HANDLE;
		multi_core_pcgp_end_auto_matchmaking();
	}
}

static void live_auto_matchmake_start()
{
	if (Auto_matchmaking_state != LIVE_AUTO_MATCHMAKING_IDLE) {
		// already running
		return;
	}

	udp_server_connection_state udp_connection_state = net_udp_server_connection_state();
	if (udp_connection_state != UDP_SERVER_CONNECTION_CONNECTED) {
		if (!net_udp_proxy_server_connect()) {
			handle_raknet_error("net_udp_proxy_server_connect", net_get_udp_error_code());
			Auto_matchmaking_state = LIVE_AUTO_MATCHMAKING_FAILED;
			return;
		}
	}

	size_t member_count = XblMultiplayerManagerLobbySessionMembersCount();
	if (member_count == 0) {
	}
	else if (member_count > 0) {
		uint64_t my_raknet_guid = net_get_rak_guid();
		SLNet::RakNetGUID guid(my_raknet_guid);
	}

	if (Login_spinner_h == INVALID_DIALOG_BOX_HANDLE) {
		dialog_box* dbp = dialog_box::create(LOCALIZE("MULTI_MATCHMAKING"), DIALOG_PRIORITY_GAME_CRITICAL);
		V_ASSERT_RETURN(dbp != NULL);
		dbp->add_spinner(LOCALIZE("MENU_SEARCHING"));
		dbp->add_option(LOCALIZE("CONTROL_CANCEL"));
		dbp->set_result_callback(auto_matchmaking_search_cancel);
		Login_spinner_h = dbp->get_handle();
	}
	Auto_matchmaking_state = LIVE_AUTO_MATCHMAKING_ACQUIRE_SESSION;
}

void live_set_game_presence();
void init_xbox_service()
{
	if (XUser_handle) {
		if (Live_context) {
			XblContextCloseHandle(Live_context);
		}

	}
	else if (Live_context) {
		XblContextCloseHandle(Live_context);
		Live_context = nullptr;
	}
}

void live_set_current_user(XUserHandle user, uint64_t xuid)
{
	if (user == nullptr) {
		if (Live_context)
		{
			XblContextCloseHandle(Live_context);
			Live_context = nullptr;
		}
		if (XUser_handle)
		{
			XUserCloseHandle(XUser_handle);
			XUser_handle = nullptr;
		}
		My_player_uid.set(0);
	}
	else if ((user && !XUser_handle) || !XUserCompare(user, XUser_handle)) {

		if (XUser_handle) {
			XUserCloseHandle(XUser_handle);
		}

		XUser_handle = user;
		My_player_uid.set(xuid);

		init_xbox_service();
	}
	else {
		XUserCloseHandle(user);
	}
}

void live_set_game_presence() {
	V_ASSERT_RETURN(Live_context != nullptr);

	XAsyncBlock* asyncBlock = Live_mempool_new<XAsyncBlock>();
	asyncBlock->queue = Live_async_queue;
	asyncBlock->callback = [](XAsyncBlock* asyncBlock)
	{
		HRESULT hr = XAsyncGetStatus(asyncBlock, false);
		if (FAILED(hr)) {
			debug_printf("multi", "Failed to set game presence.\n");
		}
		Live_mempool_delete(asyncBlock);
	};

	HRESULT hr = XblPresenceSetPresenceAsync(Live_context, true, nullptr, asyncBlock);
	if (FAILED(hr)) {
		debug_printf("multi", "Failed to set game presence async.\n");
	}
}


#if defined(__PC_GP)
#include <atomic>
std::atomic<bool> GPLdone = false;

static inline void on_lf_()
{
	MessageBoxW(
		NULL,
		(LPCWSTR)L"Failed",
		(LPCWSTR)L"Error",
		MB_ICONWARNING | MB_OK
	);
#if defined(__RELEASE_FINAL)&&0
	void _game_terminate();
	_game_terminate();
#endif
}

void CALLBACK LicenseCheck(XAsyncBlock* asyncBlock)
{
	XStoreGameLicense result{};

	HRESULT hr = XStoreQueryGameLicenseResult(asyncBlock, &result);

	if (FAILED(hr))
	{
		//Failed to retrieve a license, present error message to user and consider title unlicensed 
		release_printf("Fail 1\n");
		on_lf_();
	}

	if (!result.isActive)
	{
		//Failed to retrieve a license, present error message to user and consider title unlicensed 
		release_printf("Fail 2\n");
		on_lf_();
	}
	else {
		//Validated that the user has a real license, consider the title licensed and continue
		release_printf("Success 1\n");
	}
	GPLdone = true;

}

void IsUserLicenseValid()
{

	//If your game uses other XStore APIs beyond the one-time license check at launch, you should create the StoreContext once and reuse it throughout the game. 
	XStoreContextHandle storeContextHandle = nullptr;
	HRESULT hr = XStoreCreateContext(nullptr, &storeContextHandle);

	if (FAILED(hr)) {
		//Failed to create a store context. Most likely cause is that the game bits have been pulled from the original package and are not running as a store app. 
		//Present error to the user and consider title unlicensed 
		release_printf("Fail 3\n");
		on_lf_();
		return;
	}

	XAsyncBlock* asyncBlock = Live_mempool_new<XAsyncBlock>();
	//ZeroMemory(asyncBlock.get(), sizeof(*asyncBlock));
	asyncBlock->queue = Live_async_queue;
	asyncBlock->callback = LicenseCheck;
	asyncBlock->context = storeContextHandle;

	hr = XStoreQueryGameLicenseAsync(storeContextHandle, asyncBlock);

	if (FAILED(hr))
	{
		//Failed to query for a license. Most likely cause is that the game bits have been pulled from the original package and are not running as a store app. 
		//Present error to the user and consider title unlicensed 
		release_printf("Fail 4\n");
		on_lf_();
	}
	else {
		//Block until the callback completes and the queue empties
		while (!GPLdone.load())
			while (XTaskQueueDispatch(Live_async_queue, XTaskQueuePort::Completion, 0));
		Live_mempool_delete(asyncBlock);
	}

	//If the game users any of these resources outside the context of a basic licensing call, it should clean these up at game exit rather than here. 
	if (storeContextHandle != nullptr) {
		XStoreCloseContextHandle(storeContextHandle);
	}
}
#endif


// --------------------
//
// External Functions
//
// --------------------

void multi_core_platform_internal_system_init(v_mempool_base *pool)
{
	Xbox_live_inited = false;

	HRESULT hr = XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &Live_async_queue);
	if (FAILED(hr)) {
		// TODO: error handling
		return;
	}

	hr = XblMemSetFunctions(live_alloc_function, live_free_function);
	if (FAILED(hr)) {
		// TODO: error handling
		return;
	}
	
	uint32_t titleId = live_get_title_id();
#if defined(__RELEASE_FINAL)
	IsUserLicenseValid();	
#endif

	char scidBuffer[64] = {};
	sprintf_s(scidBuffer, "00000000-0000-0000-0000-0000%08x", titleId);

	XalInitArgs xalArgs = {};
	xalArgs.titleId = titleId;
	xalArgs.sandbox = "RETAIL";//"JTTFZP.4"; // This isn't used to control the sandbox on PC or console so you can just set this to RETAIL
	hr = XalInitialize(&xalArgs, Live_async_queue);
	if (FAILED(hr)) {
		return;
	}

	XblInitArgs xblInit = { Live_async_queue, scidBuffer };
	hr = XblInitialize(&xblInit);
	if (FAILED(hr)) {
		return;
	}

	hr = XUserRegisterForChangeEvent(Live_async_queue, nullptr, live_user_changed_callback, &Live_user_changed_event_token);
	if (FAILED(hr)) {
		debug_printf("multi", "User registration for event change failed.\n");
	}

	hr = XblMultiplayerManagerInitialize(LIVE_LOBBY_SESSION_TEMPLATE_NAME, Live_async_queue);
	if (FAILED(hr)) {
		debug_printf("multi", "Failed to initialize multiplayer manager.\n");
	}

	// Zero out my join game data
	MEMSET(&Join_info, 0, sizeof(Join_info));

	live_login_silent();

	Xbox_live_inited = true;
	Auto_matchmaking_state = LIVE_AUTO_MATCHMAKING_IDLE;
}

void multi_core_platform_internal_system_shutdown()
{
	XTaskQueueCloseHandle(Live_async_queue);
}

// Is called when a session is already established
bool live_set_matchmaking_session_data(const XblMultiplayerSessionReference* session_ref) {
	Live_session_data.reset();

	Live_session_data.is_auto_match = true;
	release_printf("Automatchmaking\n");

	size_t member_size = XblMultiplayerManagerGameSessionMembersCount();
	if (member_size != 2) {
		// WTF Microsoft!
		V_ASSERT(!"Session members count is not 2!");
		return false;
	}
	Live_session_data.members_count = member_size;
	release_printf("Member count: %i\n", member_size);

	std::vector<XblMultiplayerManagerMember> members;
	members.resize(member_size);

	HRESULT hr = XblMultiplayerManagerGameSessionMembers(member_size, members.data());
	if (FAILED(hr)) {
		V_ASSERT(!"Failed to get game session members.");
		return false;
	}

	// use the highest xuid for the host
	int host_member_index = 0;
	uint64_t highest_member_xuid = 0;
	for (int i = 0; i < member_size; ++i) {
		if (highest_member_xuid < members[i].Xuid) {
			host_member_index = i;
			highest_member_xuid = members[i].Xuid;
		}
		release_printf("Member: ID %i, XUID %llu\n", members[i].MemberId, members[i].Xuid);
	}
	XblMultiplayerManagerMember* host_member = &members[host_member_index];

	if (host_member == nullptr) {
		// no host found
		V_ASSERT(!"Failed to find the host member.");
		return false;
	}
	
	Live_session_data.host_xuid = host_member->Xuid;
	Live_session_data.is_host = Live_session_data.host_xuid == My_player_uid.get_xuid();
	release_printf("My XUID: %llu\n", My_player_uid.get_xuid());
	release_printf("Host XUID: %llu\n", Live_session_data.host_xuid);
	release_printf("I am the host: %s\n", (Live_session_data.is_host ? "true" : "false"));

	if (host_member->ConnectionAddress == nullptr || strlen(host_member->ConnectionAddress) == 0) {
		// connection address is not set
		return false;
	}

	SLNet::RakNetGUID guid;
	if (!guid.FromString(host_member->ConnectionAddress)) {
		return false;
	}

	Live_session_data.host_raknet_guid = guid.g;
	release_printf("Host raknet guid: %llu\n", guid.g);

	CoCreateGuid(&Live_session_data.session_guid);
	if (!string_to_guid(session_ref->SessionName, &Live_session_data.session_guid)) {
		return false;
	}
	release_printf("SessionName: %s\n", session_ref->SessionName);

	// get the client rak net guid
	for (int i = 0; i < member_size; ++i) {
		if (members[i].Xuid != host_member->Xuid) {
			XblMultiplayerManagerMember* client_member = &members[i];
			if (client_member->ConnectionAddress == nullptr || strlen(client_member->ConnectionAddress) == 0) {
				// client connection address is not set
				return false;
			}

			SLNet::RakNetGUID client_guid;
			if (!client_guid.FromString(client_member->ConnectionAddress)) {
				return false;
			}
			Live_session_data.client_raknet_guid = client_guid.g;
			break;
		}
	}

	return true;
}

uint64_t live_get_matchmaking_live_host_xuid()
{
	return Live_session_data.host_xuid;
}

bool multi_core_pcgp_is_auto_matchmaking()
{
	return Live_session_data.is_auto_match;
}

uint64_t live_get_matchmaking_raknet_host_guid()
{
	return Live_session_data.host_raknet_guid;
}

uint64_t live_get_matchmaking_raknet_client_guid()
{
	return Live_session_data.client_raknet_guid;
}

GUID live_get_matchmaking_session_guid()
{
	return Live_session_data.session_guid;
}

void multi_core_platform_internal_system_process()
{
	switch (Login_state) {
	case LIVE_LOGIN_STATE_HANDLE_LOGIN_RESULT:
		live_handle_login_result(Live_login_result);
	}

	if (XUser_handle != nullptr && Live_context != nullptr)
	{
		HRESULT hr = XblMultiplayerManagerDoWork(&Live_multiplayer_events, &Live_multiplayer_events_count);
		if (SUCCEEDED(hr)) {

			/*const XblMultiplayerEvent* current_event = Live_multiplayer_events;
			for (int i = 0; i < Live_multiplayer_events_count; ++i, ++current_event)
			{
				switch (current_event->EventType)
				{
				}
			}*/
		}
		else {
			debug_printf("multi", "XblMultiplayerManagerDoWork failed.\n");
		}
	}

	if (Auto_matchmaking_state == LIVE_AUTO_MATCHMAKING_ACQUIRE_SESSION) {

		XblMultiplayerMatchStatus search_status = XblMultiplayerManagerMatchStatus();
		XblMultiplayerMeasurementFailure failure_status = XblMultiplayerMeasurementFailure::None;
		if (search_status == XblMultiplayerMatchStatus::Failed) {
			handle_live_matchmaking_error("XblMultiplayerManagerMatchStatus", failure_status);
			Auto_matchmaking_state = LIVE_AUTO_MATCHMAKING_FAILED;
		}
		

		HRESULT hr;
		const XblMultiplayerEvent* current_event = Live_multiplayer_events;
		for (int i = 0; i < Live_multiplayer_events_count; ++i, ++current_event)
		{
			switch (current_event->EventType)
			{
			case XblMultiplayerEventType::UserAdded:
				if (SUCCEEDED(current_event->Result)) {
					uint64_t my_raknet_guid = net_get_rak_guid();
					SLNet::RakNetGUID guid(my_raknet_guid);
				}
				else {
					handle_live_error("XblMultiplayerManagerLobbySessionAddLocalUser_UserAdded", current_event->Result);
					Auto_matchmaking_state = LIVE_AUTO_MATCHMAKING_FAILED;
				}
				break;
			case XblMultiplayerEventType::LocalMemberConnectionAddressWriteCompleted:
				if (SUCCEEDED(current_event->Result)) {
					hr = XblMultiplayerManagerFindMatch(LIVE_DEFAULT_HOPPER_NAME, "", MATCHING_TIMEOUT_IN_SECONDS);
					if (FAILED(hr)) {
						handle_live_error("XblMultiplayerManagerFindMatch", hr);
						Auto_matchmaking_state = LIVE_AUTO_MATCHMAKING_FAILED;
					}
				}
				else {
					handle_live_error("XblMultiplayerManagerLobbySessionSetLocalMemberConnectionAddress_LocalMemberConnectionAddressWriteCompleted", current_event->Result);
					Auto_matchmaking_state = LIVE_AUTO_MATCHMAKING_FAILED;
				}
				break;
			case XblMultiplayerEventType::FindMatchCompleted:
				if (SUCCEEDED(current_event->Result)) {
					hr = XblMultiplayerEventArgsFindMatchCompleted(current_event->EventArgsHandle, &search_status, &failure_status);
					if (SUCCEEDED(hr) && search_status == XblMultiplayerMatchStatus::Completed) {
						if (live_set_matchmaking_session_data(XblMultiplayerManagerGameSessionSessionReference())) {
							// start forwarding when I am the client
							if (Live_session_data.is_host == false) {
								uint64_t rak_net_guid = live_get_matchmaking_raknet_host_guid();

								SLNet::RakNetGUID rak_net_host_guid = SLNet::RakNetGUID(rak_net_guid);
								if (net_request_udp_forwarding(&rak_net_host_guid) == false) {
									handle_raknet_error("live_get_rak_net_host_guid", net_get_udp_error_code());
									Auto_matchmaking_state = LIVE_AUTO_MATCHMAKING_FAILED;
								}
							}
						}
						else {
							handle_live_matchmaking_error("live_set_session_data", XblMultiplayerMeasurementFailure::Unknown);
							Auto_matchmaking_state = LIVE_AUTO_MATCHMAKING_FAILED;
						}
					}
					else {
						handle_live_error("XblMultiplayerEventArgsFindMatchCompleted", hr);
						Auto_matchmaking_state = LIVE_AUTO_MATCHMAKING_FAILED;
					}
				}
				else {
					handle_live_error("XblMultiplayerManagerFindMatch_FindMatchCompleted", current_event->Result);
					Auto_matchmaking_state = LIVE_AUTO_MATCHMAKING_FAILED;
				}
				break;
			default:
				// do nothing
				break;
			}
			if (Auto_matchmaking_state == LIVE_AUTO_MATCHMAKING_FAILED) {
				break;
			}
		}

		udp_server_connection_state udp_connection_state = net_udp_server_connection_state();
		udp_forwarding_state forwarding_state = get_udp_forwarding_state();

		bool done = false;

		// Does anything failed?
		if (udp_connection_state == UDP_SERVER_CONNECTION_FAILED ||
			forwarding_state == UDP_FORWARDING_FAILED ||
			Auto_matchmaking_state == LIVE_AUTO_MATCHMAKING_FAILED) {

			// Something failed, UDP connection, Live matchmaking or forwarding.
			const int udp_error_code = net_get_udp_error_code();
			if (udp_error_code != 0) {
				handle_raknet_error("Unknown", udp_error_code);
			}
			Auto_matchmaking_state = LIVE_AUTO_MATCHMAKING_FAILED;

			Auto_automatchmake_cb(AUTO_MATCHMAKING_RESULT_FAILED);
			multi_core_pcgp_end_auto_matchmaking();
			done = true;
		}
		else {
			// still running or succeeded
			bool is_udp_succeeded = (forwarding_state == UDP_FORWARDING_LISTENING && Live_session_data.is_host == true) ||
				(forwarding_state == UDP_FORWARDING_CONNECTED && Live_session_data.is_host == false);

			if (udp_connection_state == UDP_SERVER_CONNECTION_CONNECTED && is_udp_succeeded) {
				// succeeded!!!
				Auto_matchmaking_state = LIVE_AUTO_MATCHMAKING_CONNECTED;
				Auto_automatchmake_cb(Live_session_data.is_host ? AUTO_MATCHMAKING_RESULT_HOST : AUTO_MATCHMAKING_RESULT_CLIENT);
				done = true;
			}
		}

		if (done && Login_spinner_h != INVALID_DIALOG_BOX_HANDLE) {
			dialog_box *dbp = dialog_box::find(Login_spinner_h);
			V_ASSERT(dbp != NULL);
			if (dbp) {
				dbp->force_close();
			}
			Login_spinner_h = INVALID_DIALOG_BOX_HANDLE;
		}
	}
}

void multi_core_platform_internal_multi_init(v_mempool_base *pool)
{
}

void multi_core_platform_internal_multi_shutdown()
{
	multi_core_pcgp_end_auto_matchmaking();
}

void multi_core_pcgp_set_join_info(GUID session_id, GUID handle_id, uint64_t host_xuid, uint64_t invitee_xuid, bool invited)
{
	Join_info.invitee.set(invitee_xuid);
	Join_info.session_info.host.uid.set(host_xuid);

	/*GUID guid;
	string_to_guid(&session_id.value[0], &guid);*/
	Join_info.session_info.platform.session_id = session_id;
	Join_info.session_info.platform.handle_id = handle_id;
	Join_info.pending_was_invited = invited;

	// We have a pending invite now, as soon as we can, try and get information for it
	Join_info.invite_pending = false;
	Join_info.invite_failed = false;
	Join_info.invite_ready = true;
}

bool multi_core_platform_internal_have_join_info()
{
	return Join_info.invite_ready;
}

bool multi_core_platform_internal_have_pending_join_info()
{
	return Join_info.invite_pending;
}

bool multi_core_platform_internal_getting_join_info_failed()
{
	return Join_info.invite_failed;
}

uint multi_core_platform_internal_use_join_info(multi_session_info *join_info, bool &invited)
{
	if (!Join_info.invite_ready) {
		V_ASSERT(!"Do not have info to use.");
		return 0;
	}

	invited = Join_info.was_invited;
	if (join_info) {
		join_info->copy(Join_info.session_info);
	}

	Join_info.invite_ready = false;

	return sizeof(multi_session_info);
}

void multi_core_platform_internal_clear_join_info(bool failed)
{
	MEMSET(&Join_info, 0, sizeof(Join_info));
	Join_info.invite_failed = failed;
}

bool multi_core_platform_internal_query_for_match_info(multi_session_info *session_info)
{
	V_ASSERT_MSG(false, "Not implemented");
	return false;
}

bool multi_core_platform_internal_query_in_progress()
{
	// Not used
	V_ASSERT_MSG(false, "Not implemented");
	return false;
}

multi_core_platform_query_info_response multi_core_platform_internal_query_for_info_finished(multi_session_info *join_info)
{
	V_ASSERT_MSG(false, "Not implemented");
	return MULTI_QIR_FAILED;
}

bool multi_core_platform_internal_ui_is_active()
{
	return Live_UI_active ||
		Login_state == LIVE_LOGIN_STATE_LOGIN_WITH_UI ||
		Login_state == LIVE_LOGIN_STATE_RESOLVE_USER_ISSUE;
}

bool multi_core_platform_internal_is_signed_into_profile()
{
	return XUser_handle != nullptr;
}

bool multi_core_platform_internal_is_connected_to_internet()
{
	if (Xbox_live_inited && XUser_handle != nullptr) {
		XUserState state = XUserState::SignedOut;
		HRESULT hr = XUserGetState(XUser_handle, &state);
		if (SUCCEEDED(hr)) {
			return state == XUserState::SignedIn;
		}
	}
	return false;
}

bool multi_core_platform_internal_is_connected_to_network()
{
	return true;
}

bool multi_core_platform_internal_user_has_privilege(multi_core_platform_user_privilege privilege)
{
	if (!multi_core_platform_is_connected_to_internet()) {
		return false;
	}

#if defined(FAKE_USER_ENABLED)
	return true;
#endif

	bool out = false;
	HRESULT result = S_OK;
	XalPrivilegeCheckDenyReasons reason = XalPrivilegeCheckDenyReasons_None;

	switch (privilege) {
	case MULTI_USER_PRIVILEGE_ONLINE_SESSIONS:
		//result = XalUserCheckPrivilege(XUser_handle, XalPrivilege_Sessions, &out, &reason);
		result = XalUserCheckPrivilege(NULL, XalPrivilege_Multiplayer, &out, &reason);
		break;

	case MULTI_USER_PRIVILEGE_VIDEO_COMMUNICATIONS:
	case MULTI_USER_PRIVILEGE_COMMUNICATIONS:
		result = XalUserCheckPrivilege(NULL, XalPrivilege_Comms, &out, &reason);
		break;

	case MULTI_USER_PRIVILEGE_VIEW_PRIFILE:
		result = XalUserCheckPrivilege(NULL, XalPrivilege_Ugc, &out, &reason);
		break;

	case MULTI_USER_PRIVILEGE_SOCIAL_NETWORK:
		result = XalUserCheckPrivilege(NULL, XalPrivilege_SocialNetworkSharing, &out, &reason);
		break;

	default:
		return false;
	}

	if (SUCCEEDED(result)) {
		return out ? true : false;
	}

	return false;
}

bool multi_core_platform_internal_user_has_privilege_with_friend(multi_core_platform_user_privilege privilege)
{
	V_ASSERT_MSG(false, "Not implemented");
	return true;
}

void multi_core_platform_internal_set_game_type_context(platform_game_type_context context)
{
}

void multi_core_platform_internal_set_game_mode_context(uint context)
{
}

void multi_core_platform_internal_set_game_presence_context(uint context)
{
}

void multi_core_platform_internal_set_game_title_defined_context(uint context_id, uint context_value)
{
}

void multi_core_platform_internal_set_user_property(uint property_id, uint size, void *data)
{
}

void multi_core_platform_internal_guess_active_controller()
{
	multi_core_platform_set_active_controller(0);
}

void multi_core_platform_internal_set_active_controller(int controller_idx)
{
}

bool multi_core_platform_internal_get_player_name(wchar_t *player_name, int max_player_name_size)
{
	bool retrieved_player_name = false;
	if (multi_core_platform_internal_is_signed_into_profile()) {
		char temp_player_tag[128];
	}

	return retrieved_player_name;
}

bool multi_core_platform_internal_get_player_name(char *player_name, int max_player_name_size)
{
	return false;
}

void multi_core_platform_internal_award_achievement(achievement const* ach)
{
	XAsyncBlock* asyncBlock = new XAsyncBlock{};
	asyncBlock->callback = [](XAsyncBlock* asyncBlockInner)
	{
		HRESULT hr = XAsyncGetStatus(asyncBlockInner, true);

		//Log("Updated achievement");
		delete asyncBlockInner;
	};

	char tmp[256];
	sprintf(tmp, "%d", ach->xlast_achievement_id);
	HRESULT hr = XblAchievementsUpdateAchievementAsync(
		Live_context,  // The Xbox Live context handle for the user
		My_player_uid.get_xuid(),  // The Xbox Live User ID for the user
		tmp,  // The ID of the achievement you want to update/unlock
		100,  // The percentComplete, a whole number between 1-100.  To unlock an achievement, set this to 100
		asyncBlock
	);
	V_ASSERT(SUCCEEDED(hr));
}

void multi_core_platform_internal_avatar_award(int avatar_award_no)
{
}

void multi_core_platform_internal_send_invite(multi_player_uid &player_uid, const TCHAR* message, multi_session *sp)
{
	V_ASSERT_MSG(false, "Not implemented");
}

void multi_core_platform_internal_set_ui_active(bool active)
{
	V_ASSERT_MSG(false, "Not implemented");
}

void multi_core_platform_internal_start_login(bool internet_ready, multi_core_platform_signin_cb cb)
{
	V_ASSERT(Xbox_live_inited);

	if (multi_core_platform_is_connected_to_internet()) {
		if (cb != NULL) {
			cb(LOGIN_RESULT_SUCCESS);
		}
		return;
	}

	if (!multi_core_platform_internal_is_connected_to_network()) {
		if (cb != NULL) {
			cb(LOGIN_RESULT_FAILED_GENERIC);
		}
		return;
	}

	Signin_cb = cb;

	live_start_login(true);
}

void multi_core_platform_internal_start_auto_matchmake(multi_core_platform_pcgp_auto_matchmaking_cb cb)
{
	V_ASSERT(Xbox_live_inited);

	Auto_automatchmake_cb = cb;

	udp_forwarding_state forwarding_state = get_udp_forwarding_state();
	udp_server_connection_state udp_connection_state = net_udp_server_connection_state();
	if (Auto_matchmaking_state == LIVE_AUTO_MATCHMAKING_CONNECTED && 
		udp_connection_state == UDP_SERVER_CONNECTION_CONNECTED &&
		(forwarding_state == UDP_FORWARDING_LISTENING || 
			forwarding_state == UDP_FORWARDING_CONNECTED)) {

		
		Auto_automatchmake_cb(Live_session_data.is_host ? AUTO_MATCHMAKING_RESULT_HOST : AUTO_MATCHMAKING_RESULT_CLIENT);
		return;
	}

	Auto_matchmaking_state = LIVE_AUTO_MATCHMAKING_IDLE;
	Live_session_data.reset();
	reset_udp_connection_state(); // In case it failed previously

	live_auto_matchmake_start();
}

void multi_core_platform_internal_show_gamercard(multi_player_uid &player_uid)
{
	V_ASSERT(Xbox_live_inited);

	if (!multi_core_platform_is_connected_to_internet()) {
		V_ASSERT(!"Not logged in");
		return;
	}	

	XAsyncBlock* async = Live_mempool_new<XAsyncBlock>();
	async->queue = Live_async_queue;
	async->callback = [](XAsyncBlock* async) {
		HRESULT hr = XGameUiShowPlayerProfileCardResult(async);
		Live_mempool_delete(async);
		Live_UI_active = false;
	};
	
	HRESULT hr = XGameUiShowPlayerProfileCardAsync(async, XUser_handle, player_uid.get_xuid());
	if (SUCCEEDED(hr)) {
		Live_UI_active = true;
	}
	else {
		V_ASSERT(!"Something went wrong with show gamercard");
	}
}

void multi_core_platform_internal_open_friend_request_ui(multi_player_uid const &uid)
{
	V_ASSERT_MSG(false, "Not implemented");
}

//void multi_core_platform_internal_main_menu_account_picker()
//{
//	// XGameUiShowPlayerPickerAsync
//	V_ASSERT(Xbox_live_inited);
//	if (XUser_handle != nullptr) {
//		XUserCloseHandle(XUser_handle);
//	}
//	live_login_with_ui();
//}

multi_core_platform_nat_type multi_core_platform_internal_get_nat_type()
{
	return MULTI_NAT_OPEN;
}

bool multi_platform_internal_get_local_ip_address(connection_address &address)
{
	address.net_type = NT_INVALID;

	char local_host_name[256];
	int result = gethostname(local_host_name, STATIC_ARRAY_SIZE(local_host_name));
	V_ASSERT_MSG(result == ERROR_SUCCESS, "Could not get local host's name for address resolution");

	if (result == ERROR_SUCCESS) {
		hostent *local_host_entry = gethostbyname(local_host_name);

		if (local_host_entry != NULL) {
			if (local_host_entry->h_addrtype == AF_INET) {
				int i = 0;
				while (local_host_entry->h_addr_list[i] != 0) {
					IN_ADDR *local_in_addr = (IN_ADDR *)local_host_entry->h_addr_list[i++];

					if (local_in_addr->S_un.S_addr != 0x7f000001) {
						address.net_type = NT_IP;
						address.addr.address = htonl(local_in_addr->S_un.S_addr);
						address.addr.port = DEFAULT_GAME_PORT;
						// DSFL bcovacha: this is quite hacked but lets see if required
						address.addr.rak_address.systemAddress.address.addr4.sin_addr = *local_in_addr;
						address.addr.rak_address.systemAddress.address.addr4.sin_port = DEFAULT_GAME_PORT;
						return true;
					}
				}
			}
		}
	}

	return false;
}

int multi_xbox_live_get_num_players_in_party()
{
	if (Live_session_data.is_auto_match) {
		return Live_session_data.members_count;
	}
	else {
		return XblMultiplayerManagerLobbySessionMembersCount();
	}
}

uint32_t live_get_title_id()
{
	return LIVE_TITLE_ID;

	// XGameXboxTitleId return the Xbox-App title id
	/*uint32_t titleId;
#if !defined(__RELEASE_FINAL)
	titleId = LIVE_TITLE_ID;
#else
	HRESULT hr = XGameGetXboxTitleId(&titleId);
	if (FAILED(hr)) {
		titleId = LIVE_TITLE_ID;
	}
#endif
	return titleId;*/
}

bool getnumber(const char *string, int soffset, int eoffset, int shift, uint64 *ovalue)
{
	bool result = true;
	uint64 val;
	uint64 value = 0;
	for (int ii = soffset; ii <= eoffset; ii++) {
		unsigned char c = string[ii];
		if (c >= '0' && c <= '9')
		{
			val = uint64(c - '0');
			value += val << shift;
			shift -= 4;
		}
		else if (c >= 'a' && c <= 'f')
		{
			val = uint64(c - 'a' + 10);
			value += val << shift;
			shift -= 4;
		}
		else if (c >= 'A' && c <= 'F')
		{
			val = uint64(c - 'A' + 10);
			value += val << shift;
			shift -= 4;
		}
		else if (c != '-')
		{
			ii = eoffset + 1;
			result = false;
		}
	}
	if (result) {
		*ovalue = value;
	}
	return result;
}

bool string_to_guid(const char* string, GUID* guid)
{
	if (string == nullptr || guid == nullptr) {
		return false;
	}

	size_t clen = strlen(string);
	if (clen != 36) {
		return false;
	}

	unsigned long  Data1 = 0;
	unsigned short Data2 = 0;
	unsigned short Data3 = 0;
	unsigned char  Data4[8] = { 0 };
	uint64 value = 0;

	if (!getnumber(string, 0, 7, 7 * 4, &value)) {
		return false;
	}
	Data1 = (unsigned long)value;

	if (!getnumber(string, 9, 12, 3 * 4, &value)) {
		return false;
	}
	Data2 = (unsigned short)value;

	if (!getnumber(string, 14, 17, 3 * 4, &value)) {
		return false;
	}
	Data3 = (unsigned short)value;


	int soffset = 19;
	for (int ii = 0; ii < 8; ii++)
	{
		if (!getnumber(string, soffset, soffset + 1, 1 * 4, &value)) {
			return false;
		}
		Data4[ii] = (unsigned char)value;
		soffset += 2;
		if (soffset == 23) soffset++;
	}

	guid->Data1 = Data1;
	guid->Data2 = Data2;
	guid->Data3 = Data3;
	memcpy(guid->Data4, Data4, sizeof(Data4));

	return true;
}

bool guid_to_string(GUID* guid, char* buffer, size_t buffer_size)
{
	if (guid == nullptr) {
		return false;
	}
	int result = sprintf_s(buffer, buffer_size, "%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX", 
		guid->Data1, guid->Data2, guid->Data3,
		guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3],
		guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);

	return result >= 0;
}

void multi_core_pcgp_end_auto_matchmaking()
{
	if (Live_session_data.is_auto_match) {
		// leave game
		XblMultiplayerManagerLeaveGame();
	}
	
	// leave lobby
	XUserHandle user_handle;

	if (XblMultiplayerManagerMatchStatus() != XblMultiplayerMatchStatus::None) {
		XblMultiplayerManagerCancelMatch();
	}
	net_close_udp_forwarding();
	net_close_udp_proxy_server_connection();
	reset_udp_connection_state();
	Auto_matchmaking_state = LIVE_AUTO_MATCHMAKING_IDLE;
	Live_session_data.reset();
}

void handle_live_error(const char* func_name, const HRESULT result)
{
	HRESULT_CODE(result);
	std::string message = std::system_category().message(result);

#if defined (_DEBUG)
	debug_printf("%s failed with return code 0x%x %s", func_name, result, message.c_str());
#else
	release_printf("%s failed with return code 0x%x %s\n", func_name, result, message.c_str());
#endif

	// No idea what errors can occur, it is missing in the documentation

	switch (result) {
	case ERROR_CONNECTION_INVALID:
		multi_game_error_set(MULTI_GAME_ERROR_NO_JOIN_INFO);
	default:
		multi_game_error_set(MULTI_GAME_ERROR_INTERNAL_ERROR);
		break;
	}
}

void handle_raknet_error(const char* func_name, int result)
{
#if defined (_DEBUG)
	debug_printf("%s failed with return code %i", func_name, result);
#else
	release_printf("%s failed with return code %i\n", func_name, result);
#endif

	switch (result) {
	case UDP_ERROR_NO_SERVERS_ONLINE:
	case UDP_ERROR_RECIEPIENT_NOT_CONNECTED:
		multi_game_error_set(MULTI_GAME_ERROR_SESSION_ERROR);
		break;
	case UDP_ERROR_ALL_SERVERS_ARE_BUSY:
		multi_game_error_set(MULTI_GAME_ERROR_SESSION_FULL);
		break;
	case ID_CONNECTION_ATTEMPT_FAILED:
		multi_game_error_set(MULTI_GAME_ERROR_INTERNAL_ERROR);
		break;
	case UDP_ERROR_FORWARDING_TIMEOUT:
	case ID_DISCONNECTION_NOTIFICATION:
		multi_game_error_set(MULTI_GAME_ERROR_LOST_CONNECTION);
		break;
	default:
		multi_game_error_set(MULTI_GAME_ERROR_INTERNAL_ERROR);
		break;
	}
}

void handle_live_matchmaking_error(const char* func_name, XblMultiplayerMeasurementFailure result)
{
#if defined (_DEBUG)
	debug_printf("%s failed with return code %i", func_name, result);
#else
	release_printf("%s failed with return code %i\n", func_name, result);
#endif

	switch (result) {
	case XblMultiplayerMeasurementFailure::Timeout: //This player failed because timeout measurement test failed
		multi_game_error_set(MULTI_GAME_ERROR_LOST_CONNECTION);
		break;
	case XblMultiplayerMeasurementFailure::Network: // This player failed due to a network error such as the user was unreachable
		multi_game_error_set(MULTI_GAME_ERROR_SESSION_ERROR);
		break;
	case XblMultiplayerMeasurementFailure::Latency: // This player failed because latency measurement test failed
	case XblMultiplayerMeasurementFailure::BandwidthUp: // This player failed because bandwidth up measurement test failed
	case XblMultiplayerMeasurementFailure::BandwidthDown: // This player failed because bandwidth down measurement test failed
	case XblMultiplayerMeasurementFailure::Group: // This player failed cause someone failed in their group failed
	case XblMultiplayerMeasurementFailure::Episode: // This player failed because your episode failed.   This likely happened because there wasn't enough users in the session
	case XblMultiplayerMeasurementFailure::Unknown: // Unknown measurement failure
	default:
		multi_game_error_set(MULTI_GAME_ERROR_INTERNAL_ERROR);
	}
}

#endif // defined(__PCGP)
