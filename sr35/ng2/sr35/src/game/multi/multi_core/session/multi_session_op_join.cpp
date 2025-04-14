#include "multi_session_op_join.h"

#include "downloadable_content/dlc_game.h"
#include "gamewide/gamewide.h"
#include "multi/multi_core/multi_join.h"
#include "multi/multi_core/multi_core_matchmaking.h"
#include "multi/multi_game/multi_game.h"
#include "multi/multi_game/multi_game_error.h"
#if defined(__XBOX3)	// HVS_JRS 6/9/2014 check for match making
#include "vlib/durango_clr/vlive_clr.hpp"	// HVS_JRS 6/9/2014 xbox3 networking
#endif

#if defined(__NX__)
#include "multi/multi_core/multi_core_platform_internal.h"
#include "vlib/network/nx64_net.h"
#include <nex.h>
#endif

#if defined(__PC_GP)
#include "multi/multi_core/multi_core_platform_internal.h"
#include "multi/multi_core/multi_core_platform_pcgp.h"
#include <xsapi-c/services_c.h>
#include <xsapi-c/xbox_live_context_c.h>
#include <xsapi-c/types_c.h>
#include <xsapi-c/multiplayer_manager_c.h>
#endif

// --------------------
//
// Defines
//
// --------------------
#define WAIT_FOR_JOIN_TIME					(10 * 1000)

#if !defined(__PC_GP)
// We will wait a little longer for a matchmaking join, but not forever.
#define WAIT_FOR_MATCHMAKE_JOIN_TIME		(20 * 1000)
#endif

#if defined(__PC_GP)
#define WAIT_FOR_HOST_READY_TIME_MS			(120 * 1000)
#define WAIT_FOR_MATCHMAKE_JOIN_TIME		(60 * 1000)
#endif

// --------------------
//
// Classes/Structures
//
// --------------------

#if defined(__NX__)
	struct nex_join_data {
		nn::nex::CallContext ctx;
		nn::nex::ProtocolCallContext p_ctx;
		nn::nex::MatchmakeSession session;
		nn::nex::qList<nn::nex::StationURL> station_urls;
		nn::nex::StationProbeList station_probe_list;
		nn::nex::SmartDeviceVoiceChatJoinRoomResult join_room_result;
	};
#endif

// --------------------
//
// Global Variables
//
// --------------------

#if defined(__NX__)
	extern bool Voice_chat_available;
	extern nn::nex::MatchmakeExtensionClient* Matchmaking_client;
	extern nn::nex::SmartDeviceVoiceChatClient* Smart_device_voice_chat_client;
#endif

#if defined(__PC_GP)
	extern XblContextHandle Live_context;
	extern const XblMultiplayerEvent* Live_multiplayer_events;
	extern size_t Live_multiplayer_events_count;
#endif

// --------------------
//
// Local Variables
//
// --------------------

#if defined(__NX__)
	static object_pool<nex_join_data, MAX_SESSION_OPS> Nex_data_pool;
#endif

#if defined(__PC_GP)
	static SLNet::RakNetGUID rak_net_host_guid;
	static timestamp_realtime client_auto_match_wait_join_time;
	static timestamp_realtime time_for_host_get_ready;
#endif

// --------------------
//
// Internal Functions
//
// --------------------
#if defined(_DEBUG) && defined(__LIVE_ENABLED)
	static void debug_print_system_link_type(XNKID const &pxnkid)
	{
		// system_link
		bool system_link = XNetXnKidIsSystemLink(&pxnkid);

		// xbox system link
		bool xbox_system_link = XNetXnKidIsSystemLinkXbox(&pxnkid);

		// cross platform system link
		bool cross_platform_system_link = XNetXnKidIsSystemLinkXPlat(&pxnkid);

		debug_printf("mp", "syslink key id: syslink: %c, xbox_syslink: %c, cross_syslink: %c\n", 
			system_link ? 't' : 'f',
			xbox_system_link ? 't' : 'f',
			cross_platform_system_link ? 't' : 'f');
	}
#else		
	#define debug_print_system_link_type(...)
#endif

#if defined(__STEAM)
void multi_session_op_join::event_lobby_entered( LobbyEnter_t *pParam )
{
	m_steam_operation_done = true;

	switch(pParam->m_EChatRoomEnterResponse) {
	case k_EChatRoomEnterResponseSuccess:		// Success
		m_steam_operation_succeeded = true;
		break;
	case k_EChatRoomEnterResponseDoesntExist:	// Chat doesn't exist (probably closed)
	case k_EChatRoomEnterResponseNotAllowed:	// General Denied - You don't have the permissions needed to join the chat
	case k_EChatRoomEnterResponseFull:			// Chat room has reached its maximum size
	case k_EChatRoomEnterResponseError:			// Unexpected Error
	case k_EChatRoomEnterResponseBanned:		// You are banned from this chat room and may not join
	case k_EChatRoomEnterResponseLimited:		// Joining this chat is not allowed because you are a limited user (no value on account)
	default:
		m_steam_operation_succeeded = false;
		break;
	}
}
#endif

// --------------------
// 
// External Functions
//
// --------------------

// Set the next state swap
//
void multi_session_op_join::_set_state( multi_session_join_state state ) 
{
	_desired_join_state = state;
}

// process the starting info
multi_session_op_status multi_session_op_join::_enter_state()
{
	CEG_TEST_SECRET();

	multi_session_op_status ret = MULTI_SOS_FAILED;
	V_ASSERT( _join_state != _desired_join_state );
	_join_state = _desired_join_state;

	switch (_join_state) {

		// State only exists so it knows to switch from it
		case JOIN_NONE:
			break;

			// Start the system overlapped operation
		case JOIN_CREATE:
			ret = _join_system_enter();
			break;

			// On success - tell everyone else to modify (if they're busy with something else, this will fail... 
		case JOIN_POST_CREATE:
			ret = _post_create_enter();
			break;

		case JOIN_ASK_TO_JOIN:
			ret = _ask_enter();

		default:
			break;
	}

	_processing = true;
	return ret;
}

multi_session_op_status multi_session_op_join::_process_state()
{
	// Failed - so it will fail if we process on the NONE state
	multi_session_op_status ret = MULTI_SOS_FAILED;

	switch (_join_state) {
		case JOIN_NONE:
			break;

			// See if the overlapped operation has finished
		case JOIN_CREATE:
			ret = _join_system_process();
			break;

			// See if everyone has acked
		case JOIN_POST_CREATE:
			V_ASSERT(0); // Should fail or succeed immediately, no need to process
			break;

		case JOIN_ASK_TO_JOIN:
			ret = _ask_process();
			break;

		default:
			break;
	}

	return ret;
}

void multi_session_op_join::_exit_state( multi_session_op_status result )
{
	_processing = false;
	if (result == MULTI_SOS_SUCCESS) {
		switch (_join_state) {
			case JOIN_NONE:
				break;

				// See if the overlapped operation has finished
			case JOIN_CREATE:
				_join_system_exit(result);
				break;

			case JOIN_POST_CREATE:
				CEG_SELF_CHECK();
				_post_create_exit(result);
				break;

			case JOIN_ASK_TO_JOIN:
				_ask_exit(result);
				break;

			default:
				V_ASSERT(0);
				break;
		}
	}
}

// Actually create the object that I need to go forward
//
multi_session_op_status multi_session_op_join::_join_system_enter() 
{
	#if defined(__LIVE_ENABLED)
		memset( &_overlapped, 0, sizeof( _overlapped ) );

		// Save off the platform info, I'll be using it alot in this function
		multi_session_platform_info *platform = &(_session->_info.platform);
		multi_session_options *options = &(_session->_info.options);

		// Systemlink/LAN games are special - not technically a "session", per se, but just an agreement between '360s to talk to each other.
		if (options->systemlink_or_lan == true) {
			debug_print_system_link_type( platform->info.sessionID );
			INT result = XNetRegisterKey( &(platform->info.sessionID), &(platform->info.keyExchangeKey) );
			if (result == 0) {
				return MULTI_SOS_SUCCESS;

			} else {
				V_ASSERT_MSG(false, "Failed to register security exchange key for systemlink/LAN game");
			}

		} else { // not systemlink

			// Make sure the out pointer is clean
			platform->handle = INVALID_HANDLE_VALUE;

			DWORD option_flags = _session->_info.options.live_create_flags();
			DWORD result = XSessionCreate( option_flags, 
				multi_core_platform_get_active_controller(), 
				_session->get_options()->public_slots(),
				_session->get_options()->private_slots(),
				&(platform->nonce), 
				&(platform->info), 
				&_overlapped, 
				&(platform->handle) );

			debug_spew("XSessionCreate client(%x, %d, %d, %d, %x) = %x", option_flags, multi_core_platform_get_active_controller(),
				_session->get_options()->public_slots(), _session->get_options()->private_slots(), platform->nonce);

			switch(result) {
				case ERROR_SUCCESS:
					return MULTI_SOS_SUCCESS;

				case ERROR_IO_PENDING:
					return MULTI_SOS_IN_PROGRESS;

				default:
					release_printf("XSessionCreate failed with %x\n", result);
					V_ASSERT_MSG(false, "XSessionCreate() appears to have failed!");
					break;
			}
		}

	#elif defined(__PS3)

	if (_session->_info.options.systemlink_or_lan) {
		// nothing to do here for LAN games	
		return MULTI_SOS_SUCCESS;
	} else { // not syslink
		bool passed = multi_ps3_np_join_room(&(this->m_current_cb_event), _session->_info.platform.session_id);
		if (!passed) {
			//V_ASSERT_MSG(false, "multi_ps3_np_join_room failed\n");
			return MULTI_SOS_FAILED;
		}

		return MULTI_SOS_IN_PROGRESS;
	} 
	// Do I need this? Not sure what it is for... CMD 2/25/2011
//	else {
// 		multi_core_matchmake_find_one_off_async(&this->m_desired_info_ps3);
// 		this->m_platform_state = PS3_MSS_JOIN_QUERYING_PEER_NET_INFO;
// 		return MULTI_SOS_IN_PROGRESS;
//	}
//
//	return MULTI_SOS_SUCCESS;

	#elif defined(__PS4)	// HVS_JRS 2/6/2014 enable ps4 networking

	if (_session->_info.options.systemlink_or_lan) {
		// nothing to do here for LAN games	
		return MULTI_SOS_SUCCESS;
	} else { // not syslink
		bool passed = multi_ps4_np_join_room(&(this->m_current_cb_event), _session->_info.platform.session_id);
		if (!passed) {
			//V_ASSERT_MSG(false, "multi_ps4_np_join_room failed\n");
			return MULTI_SOS_FAILED;
		}

		return MULTI_SOS_IN_PROGRESS;
	} 
	// Do I need this? Not sure what it is for... CMD 2/25/2011
//	else {
// 		multi_core_matchmake_find_one_off_async(&this->m_desired_info_ps4);
// 		this->m_platform_state = PS4_MSS_JOIN_QUERYING_PEER_NET_INFO;
// 		return MULTI_SOS_IN_PROGRESS;
//	}
//
//	return MULTI_SOS_SUCCESS;

	#elif defined(__STEAM)
		// Save off the platform info, I'll be using it alot in this function
		multi_session_platform_info *platform = &(_session->_info.platform);
		multi_session_options *options = &(_session->_info.options);

		// Systemlink/LAN games are special - not technically a "session", per se.
		if (options->systemlink_or_lan == true) {
			debug_print_system_link_type( platform->m_host_steam_id64 );
			return MULTI_SOS_SUCCESS;
		} else { // not systemlink
			// Join the other lobby.
			V_ASSERT_RETURN_VALUE(SteamMatchmaking() != NULL, MULTI_SOS_FAILED);
			m_steam_operation_done = false;
			debug_spew("Attempting to join session id %I64d", platform->m_host_session_id64);
			SteamAPICall_t result = SteamMatchmaking()->JoinLobby(platform->m_host_session_id64);
			if(result == k_uAPICallInvalid) {
				V_ASSERT_MSG(false, "JoinLobby() appears to have failed!");
				return MULTI_SOS_FAILED;
			}
			m_lobby_entered.Register(this, &multi_session_op_join::event_lobby_entered);
			return MULTI_SOS_IN_PROGRESS;
		}
	#elif defined(__XBOX3)	// HVS_JRS 4/8/2014 xbox3 networking

	debug_spew("multi_session_op_join::_join_system_enter called - set to MULTI_SOS_SUCCESS");
	return MULTI_SOS_SUCCESS;	// HVS_JRS 4/8/2014 set to success for now
		
	#elif defined(__NX__)

		// Systemlink/LAN games are special - not technically a "session", per se.
		if (_session->_info.options.systemlink_or_lan == true) {
			debug_print_system_link_type(_session->_info.platform.gathering_id);
			return MULTI_SOS_SUCCESS;
		}
		else if (multi_core_platform_is_connected_to_internet()) { // not systemlink
			nex_data = Nex_data_pool.alloc();
			V_ASSERT(nex_data != NULL);
			if (nex_data == NULL) {
				return MULTI_SOS_FAILED;
			}
			return nex_joinig_start();
		}

		return MULTI_SOS_FAILED;
	#elif defined(__PC_GP)
		// we require in the lan play that we connect to the udp server before we can even start sending data.
		// so we will assume a connect first before anything else.
		if (_session->_info.options.systemlink_or_lan == true) {
			debug_print_system_link_type(_session->_info.platform.session_id);
	
			// I have not comments for this...more 'OO' please.
			short port = _session->_info.host.addr.addr.rak_address.systemAddress.GetPort(); 
			const char* address = _session->_info.host.addr.addr.rak_address.ToString(false);
 
			if (net_udp_server_connect(address, port) == false)
			{
				return MULTI_SOS_FAILED;
			}

			pcgp_state = PCGP_JOIN_STATE_UDP_SERVER_CONNECTING;
			return MULTI_SOS_IN_PROGRESS;
		}
		else if (multi_core_platform_is_connected_to_internet()) {
			return pcgp_joining_start();
		}
	
		return MULTI_SOS_FAILED;
	#endif

}

// Wait to see on how the operation finishes up
//
multi_session_op_status multi_session_op_join::_join_system_process()
{
	#if defined(__LIVE_ENABLED)
		DWORD op_result;
		DWORD result = XGetOverlappedResult( &_overlapped, &op_result, false );

		multi_session_op_status sos = MULTI_SOS_IN_PROGRESS;
		switch(result) {
			case ERROR_SUCCESS:
				V_ASSERT( op_result == ERROR_SUCCESS );

				// Alright, tell the system that I have successfully moved states
				sos = MULTI_SOS_SUCCESS;
				break;

			case ERROR_IO_PENDING:
			case ERROR_IO_INCOMPLETE:
				sos = MULTI_SOS_IN_PROGRESS;
				break;

			default:
				#if defined(_DEBUG)
					// Grab debug info.
					{
						byte buffer[512];
						DWORD sizeof_details = sizeof(XSESSION_LOCAL_DETAILS) + (MAX_PLAYERS * sizeof(XSESSION_MEMBER));
						XSESSION_LOCAL_DETAILS *details = reinterpret_cast<XSESSION_LOCAL_DETAILS *>(buffer);
						UNREFERENCED(sizeof_details);

						XSessionGetDetails( _session->_info.platform.handle, &sizeof_details, details, NULL);

						XUSER_CONTEXT context_presence;
						context_presence.dwContextId = X_CONTEXT_PRESENCE;
						DWORD get_context_presence_result = XUserGetContext(multi_core_platform_get_active_controller(), &context_presence, NULL);
						UNREFERENCED(get_context_presence_result);
						XUSER_CONTEXT context_game_type;
						context_game_type.dwContextId = X_CONTEXT_GAME_TYPE;
						DWORD get_context_game_type_result = XUserGetContext(multi_core_platform_get_active_controller(), &context_game_type, NULL);
						UNREFERENCED(get_context_game_type_result);
						XUSER_CONTEXT context_game_mode;
						context_game_mode.dwContextId = X_CONTEXT_GAME_MODE;
						DWORD get_context_game_mode_result = XUserGetContext(multi_core_platform_get_active_controller(), &context_game_mode, NULL);
						UNREFERENCED(get_context_game_mode_result);
					}
				#endif

				// Set the extended error
				set_error( MULTI_SOE_INTERNAL_ERROR );

				// Tell the system that the create failed (we do it here
				// since it's at this point that delete would usually follow
				sos = MULTI_SOS_FAILED;
				_session->trigger_event( MSE_SESSION_CREATED, &sos );
				break;
		}

		return sos;

	#elif defined(__PS3)

		// The callback has not finished yet.
		if (!m_current_cb_event.complete) {
			return MULTI_SOS_IN_PROGRESS;
		}

		if (m_current_cb_event.error < 0) {
			debug_spew("multi_session_op_join - join room failed error code 0x%x\n", m_current_cb_event.error);
			return MULTI_SOS_FAILED;
		}

	return MULTI_SOS_SUCCESS;

	#elif defined(__PS4)	// HVS_JRS 2/6/2014 enable ps4 networking

		// The callback has not finished yet.
		if (!m_current_cb_event.complete) {
			return MULTI_SOS_IN_PROGRESS;
		}

		if (m_current_cb_event.error < 0) {
			debug_spew("multi_session_op_join - join room failed error code 0x%x\n", m_current_cb_event.error);
			return MULTI_SOS_FAILED;
		}

	return MULTI_SOS_SUCCESS;

	#elif defined(__PC) && !defined(__PC_GP)
		if(m_steam_operation_done) {
			multi_session_options *options = &(_session->_info.options);
			if(options->systemlink_or_lan == false) {
				m_lobby_entered.Unregister();
			}
			if(!m_steam_operation_succeeded) {
				return MULTI_SOS_FAILED;
			}
			return MULTI_SOS_SUCCESS;
		}
		return MULTI_SOS_IN_PROGRESS;

	#elif defined(__XBOX3)	// HVS_JRS 4/8/2014 xbox3 networking

	return MULTI_SOS_SUCCESS;	// HVS_JRS 4/8/2014 set to success for now

	#elif defined(__NX__)
		// Systemlink/LAN games are special - not technically a "session", 
		// per se, but just an agreement between PS3's to talk to each other.
		if (_session->_info.options.systemlink_or_lan == true) {
			return MULTI_SOS_SUCCESS;
		}
		else if (nex_data != NULL) {
			switch (nex_state) {
			case NEX_JOIN_STATE_JOINING:
				return nex_joinig_process();
			case NEX_JOIN_STATE_START_NAT:
				return nex_nat_process();
			case NEX_JOIN_STATE_GET_URLS:
				return nex_get_urls_process();
			case NEX_JOIN_STATE_PROBE:
				return nex_probe_process();				
			case NEX_JOIN_STATE_VOICE_JOIN:
				return nex_voice_chat_process();
			case NEX_JOIN_STATE_NONE:
			default:
				V_ASSERT_MSG(false, "Invalid state");
				break;
			}
		}
		return MULTI_SOS_FAILED;
	#elif defined(__PC_GP)
		if (_session->_info.options.systemlink_or_lan == true) {
			switch (pcgp_state) {
			case PCGP_JOIN_STATE_UDP_SERVER_CONNECTING:
				return pcgp_udp_connection_process();
			case PCGP_JOIN_STATE_NONE:
			default:
				V_ASSERT_MSG(false, "Invalid state");
				break;
			}
		}
		else {
			switch (pcgp_state)
			{
			case PCGP_JOIN_STATE_JOINING:
				return pcgp_joinig_process();
			case PCGP_JOIN_STATE_FETCHING_CONNECTION_ADDRESS:
				return pcgp_fetching_connection_address_process();
			case PCGP_JOIN_STATE_UDP_SERVER_CONNECTING:
				return pcgp_udp_connection_process();
			case PCGP_JOIN_STATE_UDP_FORWARDING:
				return pcgp_udp_forwarding_process();
			case PCGP_JOIN_STATE_NONE:
			default:
				V_ASSERT_MSG(false, "Invalid state");
				break;
			}
		}
		return MULTI_SOS_FAILED;

	#else

	return MULTI_SOS_FAILED;

	#endif
}

#if defined(__NX__)

multi_session_op_status multi_session_op_join::nex_joinig_start()
{
	V_ASSERT_RETURN_VALUE(nex_data->p_ctx.GetState() == nn::nex::CallContext::Available, MULTI_SOS_FAILED);

	nn::nex::MatchmakeSession* auto_matchmaking_session = multi_core_nx64_get_auto_matchmaking_session();
	if (auto_matchmaking_session != NULL) {
		nex_data->session = *auto_matchmaking_session;
		_session->_pending_options.social_type = MULTI_SST_PUBLIC;
		// Do not create a new session, we have already a hosting auto matchmaking session
		_session->_info.platform.gathering_id = auto_matchmaking_session->GetID();
		nn::nex::Network::GetInstance()->SetP2PDataPacketSessionSignatureKey(auto_matchmaking_session->GetSessionKey());
		nn::nex::Stream::GetSettings(nn::nex::Stream::Game).SetIsEncryptionRequired(true);
		nn::nex::Stream::GetSettings(nn::nex::Stream::Game).SetEncryptionKey(auto_matchmaking_session->GetSessionKey());
		return nex_nat_start();
	}

	nn::nex::JoinMatchmakeSessionParam join_param;
	join_param.SetTargetGatheringId(_session->_info.platform.gathering_id);

	// Join the other session.
	if (!Matchmaking_client->JoinMatchmakeSession(&nex_data->p_ctx, join_param, &nex_data->session)) {
		V_ASSERT_RELEASE(false); // should never happen
		return MULTI_SOS_FAILED;
	}
	nex_state = NEX_JOIN_STATE_JOINING;
	return MULTI_SOS_IN_PROGRESS;
}

multi_session_op_status multi_session_op_join::nex_joinig_process()
{
	switch (nex_data->p_ctx.GetState()) {
	case nn::nex::CallContext::CallInProgress:
		return MULTI_SOS_IN_PROGRESS;
	case nn::nex::CallContext::CallFailure:
	case nn::nex::CallContext::CallCancelled:
		handle_nex_error("nn::nex::JoinMatchmakeSession()", nex_data->p_ctx.GetOutcome());
		nex_data->p_ctx.Reset();
		return MULTI_SOS_FAILED;
	case nn::nex::CallContext::CallSuccess:
		nex_data->p_ctx.Reset();
		nn::nex::Network::GetInstance()->SetP2PDataPacketSessionSignatureKey(nex_data->session.GetSessionKey());
		nn::nex::Stream::GetSettings(nn::nex::Stream::Game).SetIsEncryptionRequired(true);
		nn::nex::Stream::GetSettings(nn::nex::Stream::Game).SetEncryptionKey(nex_data->session.GetSessionKey());		
		return nex_nat_start();
	default:
		V_ASSERT_MSG(false, "Invalid state");
	}
	return MULTI_SOS_FAILED;
}

multi_session_op_status multi_session_op_join::nex_nat_start()
{
	V_ASSERT_RETURN_VALUE(nex_data->ctx.GetState() == nn::nex::CallContext::Available, MULTI_SOS_FAILED);

	if (!nn::nex::ConnectivityManager::GetInstance()->StartNATSession(&nex_data->ctx)) {
		V_ASSERT_RELEASE(false); // should never happen
		return MULTI_SOS_FAILED;
	}
	nex_state = NEX_JOIN_STATE_START_NAT;
	return MULTI_SOS_IN_PROGRESS;
}

multi_session_op_status multi_session_op_join::nex_nat_process()
{
	switch (nex_data->ctx.GetState()) {
	case nn::nex::CallContext::CallInProgress:
		return MULTI_SOS_IN_PROGRESS;
	case nn::nex::CallContext::CallFailure:
	case nn::nex::CallContext::CallCancelled:
		handle_nex_error("nn::nex::ConnectivityManager::StartNATSession()", nex_data->ctx.GetOutcome());
		nex_data->ctx.Reset();
		return MULTI_SOS_FAILED;
	case nn::nex::CallContext::CallSuccess:
		nex_data->ctx.Reset();
		return nex_get_urls_start();
	default:
		V_ASSERT_MSG(false, "Invalid state");
	}
	return MULTI_SOS_FAILED;
}

multi_session_op_status multi_session_op_join::nex_get_urls_start()
{
	V_ASSERT_RETURN_VALUE(nex_data->p_ctx.GetState() == nn::nex::CallContext::Available, MULTI_SOS_FAILED);

	nn::nex::qList<nn::nex::StationURL> lstTargetURLs;
	nex_data->station_urls.clear();
	if (!Matchmaking_client->GetSessionURLs(&nex_data->p_ctx, _session->_info.platform.gathering_id, &nex_data->station_urls)) {
		V_ASSERT_RELEASE(false); // should never happen
		return MULTI_SOS_FAILED;
	}
	nex_state = NEX_JOIN_STATE_GET_URLS;
	return MULTI_SOS_IN_PROGRESS;
}

multi_session_op_status multi_session_op_join::nex_get_urls_process()
{
	switch (nex_data->p_ctx.GetState()) {
	case nn::nex::CallContext::CallInProgress:
		return MULTI_SOS_IN_PROGRESS;
	case nn::nex::CallContext::CallFailure:
	case nn::nex::CallContext::CallCancelled:
		handle_nex_error("nn::nex::MatchMakingClient::GetSessionURLs()", nex_data->ctx.GetOutcome());
		nex_data->p_ctx.Reset();
		return MULTI_SOS_FAILED;
	case nn::nex::CallContext::CallSuccess:
		nex_data->p_ctx.Reset();
#if defined(_DEBUG)
		for (const nn::nex::StationURL& station_url : nex_data->station_urls) {
			debug_spew("Host url = %s\n", station_url.CStr());
		}
#endif
		return nex_probe_start();
	default:
		V_ASSERT_MSG(false, "Invalid state");
	}
	return MULTI_SOS_FAILED;
}

multi_session_op_status multi_session_op_join::nex_probe_start()
{
	V_ASSERT_RETURN_VALUE(nex_data->ctx.GetState() == nn::nex::CallContext::Available, MULTI_SOS_FAILED);

	nex_data->station_probe_list.clear();
	for (const nn::nex::StationURL& station_url : nex_data->station_urls) {
		nex_data->station_probe_list.AddStationURL(station_url);
	}
	if (!nn::nex::VSocket::ProbeStations(&nex_data->ctx, &nex_data->station_probe_list, 0, false)) {
		V_ASSERT_RELEASE(false); // should never happen
		return MULTI_SOS_FAILED;
	}
	nex_state = NEX_JOIN_STATE_PROBE;
	return MULTI_SOS_IN_PROGRESS;
}

multi_session_op_status multi_session_op_join::nex_probe_process()
{
	switch (nex_data->ctx.GetState()) {
	case nn::nex::CallContext::CallInProgress:
		return MULTI_SOS_IN_PROGRESS;
	case nn::nex::CallContext::CallFailure:
	case nn::nex::CallContext::CallCancelled:
		handle_nex_error("nn::nex::VSocket::ProbeStations()", nex_data->ctx.GetOutcome());
		nex_data->ctx.Reset();
		return MULTI_SOS_FAILED;
	case nn::nex::CallContext::CallSuccess:
		nex_data->ctx.Reset();
		debug_spew("NAT traversal suceeded. Ping time to game session = %ums\n", nex_data->station_probe_list.GetPingStats().GetMax());
		if (Voice_chat_available) {
			return nex_voice_chat_start();
		}
		else {
			return nex_p2p_start();
		}
	default:
		V_ASSERT_MSG(false, "Invalid state");
	}
	return MULTI_SOS_FAILED;
}

multi_session_op_status multi_session_op_join::nex_voice_chat_start()
{
	V_ASSERT_RETURN_VALUE(nex_data->p_ctx.GetState() == nn::nex::CallContext::Available, MULTI_SOS_FAILED);
	    
	nn::nex::SmartDeviceVoiceChatJoinRoomParam param;
    param.SetSessionId(_session->_info.platform.gathering_id);
    param.SetGameMode(MATCHMAKE_GAME_MODE);
	if (!Smart_device_voice_chat_client->JoinRoom(&nex_data->p_ctx, param, &nex_data->join_room_result)) {
		V_ASSERT_RELEASE(false); // should never happen
		return MULTI_SOS_FAILED;
	}
	nex_state = NEX_JOIN_STATE_VOICE_JOIN;
	return MULTI_SOS_IN_PROGRESS;	
}

multi_session_op_status multi_session_op_join::nex_voice_chat_process()
{
	switch (nex_data->p_ctx.GetState()) {
	case nn::nex::CallContext::CallInProgress:
		return MULTI_SOS_IN_PROGRESS;
	case nn::nex::CallContext::CallFailure:
	case nn::nex::CallContext::CallCancelled:
		multi_core_nx64_handle_voice_chat_error("nn::nex::SmartDeviceVoiceChatClient::JoinRoom()", nex_data->p_ctx.GetOutcome());
		nex_data->p_ctx.Reset();
		return nex_p2p_start(); 
	case nn::nex::CallContext::CallSuccess:
		_session->_info.platform.voice_chat_room_id = nex_data->join_room_result.GetRoomId();
		nex_data->p_ctx.Reset();
		return nex_p2p_start(); 
	default:
		V_ASSERT_MSG(false, "Invalid state");
	}
	return MULTI_SOS_FAILED;
}

multi_session_op_status multi_session_op_join::nex_p2p_start()
{
	if (nex_data->station_probe_list.empty()) {
		V_ASSERT_MSG(false, "Failed to get host address");
		return nex_p2p_failed();
	}

	const nn::nex::StationURL& station_url = nex_data->station_probe_list.front().GetPreferredURL();
	debug_spew("Using host address %s\n", station_url.CStr());

	_session->_info.host.addr.addr.address = station_url.GetInetAddress().GetAddress();
	_session->_info.host.addr.addr.port = station_url.GetInetAddress().GetPortNumber();

	if (!nex_p2p_socket_open()) {
		return nex_p2p_failed();
	}

	nex_state = NEX_JOIN_STATE_NONE;
	return MULTI_SOS_SUCCESS;
}

multi_session_op_status multi_session_op_join::nex_p2p_failed()
{
	if (_session->_info.platform.voice_chat_room_id != 0) {
		nn::nex::SmartDeviceVoiceChatLeaveRoomParam param;
		param.SetRoomId(_session->_info.platform.voice_chat_room_id);
		Smart_device_voice_chat_client->LeaveRoom(param);
	}	
	nn::nex::ConnectivityManager::GetInstance()->StopNATSession();
	multi_core_nx64_start_logout();	// Logout to clean up sessions
	return MULTI_SOS_FAILED;
}

void multi_session_op_join::handle_nex_error(const char* func_name, const nn::nex::qResult& result)
{
	if (nex_state >= NEX_JOIN_STATE_START_NAT) {
		nn::nex::ConnectivityManager::GetInstance()->StopNATSession();
	}
	multi_session_operation::handle_nex_error(func_name, result);
	multi_core_nx64_start_logout();	// Logout to clean up sessions
}

#endif // defined(__NX__)

#if defined(__PC_GP)
multi_session_op_status multi_session_op_join::pcgp_joining_start()
{
	bool is_auto_matchmaking = multi_core_pcgp_is_auto_matchmaking();
	if (is_auto_matchmaking) {
		// Wait some seconds to make sure the host is ready.
		//client_auto_match_wait_join_time.set(WAIT_FOR_HOST_READY_TIME_MS);
		time_for_host_get_ready.set(WAIT_FOR_HOST_READY_TIME_MS);
	}
	else {
		char guidString[128] = {};
		if (!guid_to_string(&_session->_info.platform.handle_id, &guidString[0], 128)) {
			V_ASSERT_MSG(false, "Failed to convert the session id to a string.");
			return MULTI_SOS_FAILED;
		}

	}

	pcgp_state = PCGP_JOIN_STATE_JOINING;

	return MULTI_SOS_IN_PROGRESS;
}

multi_session_op_status multi_session_op_join::pcgp_joinig_process()
{
	bool is_auto_matchmaking = multi_core_pcgp_is_auto_matchmaking();
	if (is_auto_matchmaking) {
		// Wait until the host is ready with creating a game.
		if (time_for_host_get_ready.valid() && time_for_host_get_ready.elapsed()) {
			handle_raknet_error("HostNotReadyTimeout", UDP_ERROR_FORWARDING_TIMEOUT);
			return MULTI_SOS_FAILED;
		}

		if (net_udp_is_host_ready()) {
			_session->_pending_options.social_type = MULTI_SST_PUBLIC;
			// Do not create a new session, we have already a hosting auto matchmaking session
			_session->_info.platform.session_id = live_get_matchmaking_session_guid();

			client_auto_match_wait_join_time.invalidate();
			pcgp_state = PCGP_JOIN_STATE_FETCHING_CONNECTION_ADDRESS;
			return pcgp_fetching_connection_address_start();
		}
	}
	else {
		const XblMultiplayerEvent* current_event = Live_multiplayer_events;
		for (int i = 0; i < Live_multiplayer_events_count; ++i, ++current_event) {
			switch (current_event->EventType)
			{
			case XblMultiplayerEventType::JoinLobbyCompleted:
				if (SUCCEEDED(current_event->Result)) {
					pcgp_state = PCGP_JOIN_STATE_FETCHING_CONNECTION_ADDRESS;
					return pcgp_fetching_connection_address_start();
				}
				else {
					handle_live_error("XblMultiplayerManagerJoinLobby_JoinLobbyCompleted", current_event->Result);
					return MULTI_SOS_FAILED;
				}
				break;
			default:
				// Do nothing.
				break;
			}
		}
	}

	return MULTI_SOS_IN_PROGRESS;
}

multi_session_op_status multi_session_op_join::pcgp_fetching_connection_address_start()
{
	bool is_auto_matchmaking = multi_core_pcgp_is_auto_matchmaking();
	if (is_auto_matchmaking) {
		uint64_t rak_net_guid = live_get_matchmaking_raknet_host_guid();
		rak_net_host_guid = SLNet::RakNetGUID(rak_net_guid);
		_session->_info.host.addr.addr.rak_address = rak_net_host_guid;
		pcgp_state = PCGP_JOIN_STATE_NONE;
		return MULTI_SOS_SUCCESS;
	}

	size_t member_size = XblMultiplayerManagerLobbySessionMembersCount();
	std::vector<XblMultiplayerManagerMember> members;
	members.resize(member_size);

	HRESULT hr = XblMultiplayerManagerLobbySessionMembers(member_size, members.data());
	if (FAILED(hr)) {
		handle_live_error("XblMultiplayerManagerLobbySessionMembers", hr);
		return MULTI_SOS_FAILED;
	}

	const char* connectionAddress = nullptr;
	for (const auto member : members) {
		// I am joining, check for the other players connection address
		if (member.Xuid != My_player_uid.get_xuid()) {
			// get the host connection address / raknet guid
			connectionAddress = member.ConnectionAddress;
			break;
		}
	}

	if (connectionAddress == nullptr || strlen(connectionAddress) == 0) {
		handle_live_error("connectionAddress", ERROR_CONNECTION_INVALID);
		return MULTI_SOS_FAILED;
	}

	if (!rak_net_host_guid.FromString(connectionAddress)) {
		handle_live_error("guid.FromString", ERROR_CONNECTION_INVALID);
		return MULTI_SOS_FAILED;
	}

	return MULTI_SOS_IN_PROGRESS;
}

multi_session_op_status multi_session_op_join::pcgp_fetching_connection_address_process()
{
	// just start the next step
	pcgp_state = PCGP_JOIN_STATE_UDP_SERVER_CONNECTING;
	return pcgp_udp_connection_start();
}

multi_session_op_status multi_session_op_join::pcgp_udp_connection_start()
{
	if (net_udp_proxy_server_connect() == false) {
		handle_raknet_error("net_udp_proxy_server_connect", net_get_udp_error_code());
		return MULTI_SOS_FAILED;
	}

	return MULTI_SOS_IN_PROGRESS;
}

multi_session_op_status multi_session_op_join::pcgp_udp_connection_process()
{
	udp_server_connection_state state = net_udp_server_connection_state();
	switch (state) {
	case UDP_SERVER_CONNECTION_CONNECTED:
		if (_session->_info.options.systemlink_or_lan) {
			pcgp_state = PCGP_JOIN_STATE_NONE;
		//	_session->_info.host.addr.addr.rak_address = rak_net_host_guid;
			return MULTI_SOS_SUCCESS;
		}
		else {
			pcgp_state = PCGP_JOIN_STATE_UDP_FORWARDING;
			return pcgp_udp_forwarding_start();
		}
	case UDP_SERVER_CONNECTION_NONE:
	case UDP_SERVER_CONNECTION_FAILED:
		handle_raknet_error("net_udp_server_connection_state", net_get_udp_error_code());
		return MULTI_SOS_FAILED;
	}

	return MULTI_SOS_IN_PROGRESS;
}

multi_session_op_status multi_session_op_join::pcgp_udp_forwarding_start()
{
	if (!net_request_udp_forwarding(&rak_net_host_guid)) {
		handle_raknet_error("net_request_udp_forwarding", net_get_udp_error_code());
		return MULTI_SOS_FAILED;
	}

	return MULTI_SOS_IN_PROGRESS;
}

multi_session_op_status multi_session_op_join::pcgp_udp_forwarding_process()
{
	udp_forwarding_state state = get_udp_forwarding_state();
	switch (state)
	{
	case UDP_FORWARDING_CONNECTED:
		pcgp_state = PCGP_JOIN_STATE_NONE;
		_session->_info.host.addr.addr.rak_address = rak_net_host_guid;
		return MULTI_SOS_SUCCESS;
	case UDP_FORWARDING_NONE:
	case UDP_FORWARDING_FAILED:
		handle_raknet_error("get_udp_forwarding_state", net_get_udp_error_code());
		return MULTI_SOS_FAILED;
	}

	return MULTI_SOS_IN_PROGRESS;
}
#endif

// Clean up the handle if need be on failed, otherwise set up all the important information
//
void multi_session_op_join::_join_system_exit( multi_session_op_status result )
{
#if defined(__STEAM)
	multi_session_options *options = &(_session->_info.options);
	if(options->systemlink_or_lan == false) {
		m_lobby_entered.Unregister();
	}
#endif

#if defined(__LIVE_ENABLED)
		memset( &_overlapped, 0, sizeof(XOVERLAPPED) );
	#endif

	if (result == MULTI_SOS_FAILED) {
		#if defined(__LIVE_ENABLED)
			multi_session_platform_info *platform = &(_session->_info.platform);
			if (platform->handle != INVALID_HANDLE_VALUE) {
				CloseHandle( platform->handle );
				platform->handle = INVALID_HANDLE_VALUE;
			}
		#endif

		// Failed, tell them
		_session->trigger_event( MSE_SESSION_CREATED, &result );

	} else if (result == MULTI_SOS_SUCCESS) {
		_session->_state = MULTI_SS_ALIVE;	// Oh noes, I'm alive!

		// Life is good, let's see if I can stay
		_set_state( JOIN_POST_CREATE );
	}	
}

// We've finished creating - allocate the host, we're going to be asking him shortly what we should be doing
//
multi_session_op_status multi_session_op_join::_post_create_enter() 
{
#if defined(__STEAM) && !defined(__PC_GP)
	CSteamID lobby_id(_session->_info.platform.m_host_session_id64);
	_session->_info.platform.m_host_steam_id64 = SteamMatchmaking()->GetLobbyOwner(lobby_id).ConvertToUint64();
	_session->_info.host.uid.set(lobby_id);
	_session->_info.host.addr.addr.steam_id64 = _session->_info.platform.m_host_steam_id64;
	_session->_info.host.addr.net_type = NT_IP;
	_session->_info.host.addr.addr.conn_id = INVALID_CONNECTION_ID;
#endif

	connection_address host_address, my_address;
	if ( (_session->get_my_connection_address(my_address) == MULTI_SOS_FAILED) 
		|| (_session->get_host_connection_address(host_address) == MULTI_SOS_FAILED) ) {

		return MULTI_SOS_FAILED;
	}

	if (_session->get_options()->systemlink_or_lan == false) {
		V_ASSERT( My_player_uid.is_equal( _session->get_host_info()->uid ) == false );
	}

	// Reset myself
	V_ASSERT( _session->in_dead_state() == false ); // we're always alive if we're joining
	// We'll get a real index for our me connection from the accept packet.
	multi_connection_reset( _session->_me_connection, INVALID_CONNECTION_ID, &my_address, &My_player_uid );
	_session->_me_connection->connect();

	_session->add_connection_user(_session->_me_connection); // Safe to do now for me connection. Host will have to wait.

	// Allocate the host
	multi_session_host_info *host = &(_session->_info.host);
	// We'll get a real host index from the accept packet. This one may or may not be real.
	_session->_host_connection = multi_connection_allocate( &host_address, host->index, _session, &(host->uid), false );
	V_ASSERT_RETURN_VALUE( _session->_host_connection != NULL, MULTI_SOS_FAILED );
	_session->_host_connection->connect();
	_session->add_connection( _session->_host_connection );

	// Register, but do not yet join - we join when I notice all the connections I know about are registered
	// This will make sure that I have an accurate state of all the systems before I join, and not mess up any ownerships
	
	// I can not register any system until I get my join accept with my index

	return MULTI_SOS_SUCCESS;
}

// If we successfully allocated the host and got an address, ew're going to move on to asking
//
void multi_session_op_join::_post_create_exit( multi_session_op_status result )
{
	if (result == MULTI_SOS_SUCCESS) {
		_set_state( JOIN_ASK_TO_JOIN );

	} else {
		if ((_session->_host_connection != NULL) && (_session->_host_connection->is_me() == false)) {
			_session->remove_connection(_session->_host_connection);
			_session->_host_connection = NULL;
		}
	}
}

// Ask the server if I can actually join
//
multi_session_op_status multi_session_op_join::_ask_enter()
{
	// Send a join request - set all the bools to 0 
	_nonce = (uint32)(rand());

	// The host is going to send a join response, his systems, and then the connections
	// So I know I'm done when I've received the connections - everyone is registered, and _join_status is MSJS_ACCEPTED
	_received_connections = false;
	_join_status = MSJS_WAITING_ON_RESPONSE;
	// Don't set a timeout if we are rejoining our old session.
	if (rejoining == false) {
		int32 wait_join_time = multi_game_is_matchmaking() ? WAIT_FOR_MATCHMAKE_JOIN_TIME : WAIT_FOR_JOIN_TIME;
#if defined(__PC_GP)
		if (multi_core_pcgp_is_auto_matchmaking()) {
			wait_join_time = WAIT_FOR_MATCHMAKE_JOIN_TIME;
		}
#endif
#if defined(__XBOX3)	// HVS_JRS 6/9/2014 check for match making
		if (!LiveCLR::IsProcessingOnMatchFound())
#endif
		_timeout.set(wait_join_time);
	}

	multi_join_send_join_request_packet( _session, _nonce, invited );
	return MULTI_SOS_IN_PROGRESS;
}

// wait for a response from the server and being updated by all the other users
//
multi_session_op_status multi_session_op_join::_ask_process()
{
	if (_join_status == MSJS_JOIN_ACCEPTED) {
		if (_received_connections && (_session->_system != NULL) && _session->_system->everyone_registered()
			&& (_session->_system->get_owner() != NULL)) {
			return MULTI_SOS_SUCCESS;
		} else if ((_session->_host_connection == NULL) || (_session->get_host_connection()->is_disconnected())) {
			_join_status = MSJS_JOIN_DENY_FAILED_TO_CONNECT;
			return MULTI_SOS_FAILED;
		} else {
			return MULTI_SOS_IN_PROGRESS;
		}

	} else if (_join_status == MSJS_WAITING_ON_RESPONSE) {
		
		if (_timeout.elapsed() == true) {
			_timeout.invalidate();
			_join_status = MSJS_JOIN_DENY_TIMEOUT;
			return MULTI_SOS_FAILED;
		} else {
			return MULTI_SOS_IN_PROGRESS;
		}

	} else {
		return MULTI_SOS_FAILED;
	}
}

// handled above in the process state
//
void multi_session_op_join::_ask_exit( multi_session_op_status result ) 
{
	if (result == MULTI_SOS_SUCCESS) {
		_session->get_system()->join( MULTI_SSS_JOINED );
		multi_synced_system_send_full_update_to( NULL, _session );
	}
	// else, it'll delete - cleaning up the rest
}


multi_session_op_join::multi_session_op_join() 
	: multi_session_operation( SESSION_OP_JOIN )
{
	#if defined(__LIVE_ENABLED)
		memset( &_overlapped, 0, sizeof(XOVERLAPPED) );
	#endif

	#if defined(__NX__)
		nex_state = NEX_JOIN_STATE_NONE;
		nex_data = NULL;
	#endif

	_join_state = JOIN_NONE; 
	_desired_join_state = JOIN_NONE;
	_processing = false;

	_join_status = MSJS_JOIN_DENY_SYSTEM;
	_timeout.invalidate();
	_received_connections = false;

	invited = false;
	rejoining = false;
}

// First part of the accept process
// Tells me my index and also tells me the rest of the information on my host
//
void multi_session_op_join::process_accept( multi_packet const &packet, sender const &from )
{
	V_ASSERT_RETURN( _join_status == MSJS_WAITING_ON_RESPONSE );
	debug_spew( "Join accepted" );

	uint8 index = packet.get<uint8>();
	uint8 host_system_id = packet.get<uint8>();

	// Alright, get the host info, and register my system
	multi_connection *host = _session->get_host_connection();

	V_ASSERT( from.connection == host );
	uint8 host_index = packet.get<uint8>();
	host->set_index( host_index );
	
	host->player_uid.read_from_packet( packet );
	host->read_connection_data_from_packet( packet );
	host->read_name_from_packet( packet );

	// Read DLC bundles.
	dlc_bundle_bit_array bundle_bit_array;
	packet.read_bytes(bundle_bit_array.get_buffer(), DLC_BUNDLE_BIT_ARRAY_SIZE);
	dlc_game_set_coop_partner_bundles(bundle_bit_array);
	
	// Now that we have the remote player's uid it is safe to add them to the connection.
	_session->add_connection_user(host);

	// Register the index and system
	_session->get_me_connection()->set_index( index );
	_session->_register_system( host_system_id );

	// Finally, set the time
	uint32 raw_time = packet.get<uint>();
	uint32 raw_realtime = packet.get<uint>();
	_session->_init_clock( raw_time, raw_realtime );
	_session->_process_clock_packet( raw_time, raw_realtime );

	_join_status = MSJS_JOIN_ACCEPTED;
	
}

void multi_session_op_join::process_deny( multi_packet const &packet, sender const &from )
{
	V_ASSERT( _join_status != MSJS_JOIN_ACCEPTED );
	debug_spew( "Join denied." );

	packet.read<multi_session_join_status>( _join_status );

	// Don't bother with this message if we are in the middle of matchmaking.
	if (multi_game_is_matchmaking() == false) {
		if (_join_status == MSJS_JOIN_DENY_VERSION) {
			multi_game_error_set(MULTI_GAME_ERROR_VERSION_MISMATCH);
		} else {
			multi_game_error_set(MULTI_GAME_ERROR_JOIN_REJECTED);
		}
	}
}

void multi_session_op_join::process_keep_alive( multi_packet const &packet, sender const &from )
{
	V_ASSERT( _join_status == MSJS_WAITING_ON_RESPONSE );

	multi_connection* me_connection = _session->get_me_connection();

	uint8 index = packet.get<uint8>();

	// Set the index on our connection so it will start accepting heartbeat packets on the PS3.
	if (me_connection && me_connection->index_in_session == INVALID_CONNECTION_ID) {
		me_connection->set_index( index );
	}

	bool accepted = packet.get<bool>();

	// Don't reset our timer if we are matchmaking. This will cause Matchmaking to time out eventually.
	// Unless the other side has accepted the connection, they are just in the middle of a mission or something.
	if (multi_game_is_matchmaking() == false || accepted == true) {
		_timeout.set( WAIT_FOR_JOIN_TIME );
	}
	debug_spew( "Received a wait message from join system" );
}

void multi_session_op_join::process_connection_list( multi_packet const &packet, sender const &from )
{
	V_ASSERT( _join_status == MSJS_JOIN_ACCEPTED );
	debug_spew( "Received connection list" );

	uint8 num_connections = packet.get<uint8>();

	for (uint8 i = 0; i < num_connections; i++) {
		multi_join_process_new_connection( packet, from );
	}

	_received_connections = true;
}


// Make sure I'm just allocated - no currently joined
//
bool multi_session_op_join::can_start()
{
	return (_session->_state == MULTI_SS_ALLOCATED);
}

// 
multi_session_op_status multi_session_op_join::start()
{
	// I need to set desired state here - so that the priority system doesn't try to host before 
	// a join can start - we can only join if we're dead on the allocated state - so this is valid and
	// won't break anything
	V_ASSERT( _session->_desired_state == MULTI_SS_ALLOCATED );
	_session->_desired_state = MULTI_SS_ALIVE;

	// Just set the handle to invalid - since I don't want to use our hosts handle
	#if defined(__LIVE_ENABLED)
		_session->_info.platform.handle = INVALID_HANDLE_VALUE;
	#elif defined(__STEAM)
	#elif defined(__XBOX3)	// HVS_JRS 4/14/2014 setup already
	#else
		// Not valid for non-systemlink games - we'll find it later
		if (_session->get_options()->systemlink_or_lan == false) {
			_session->_info.host.addr.init();
		}
	#endif

	_set_state( JOIN_CREATE );
	return process();
}

multi_session_op_status multi_session_op_join::process()
{
	multi_session_op_status result = MULTI_SOS_FAILED;
	
	// If we're changing states - last check is just to make sure we don't double process something on the same frame
	// So I can do an unlimited number of state switches, but only one process, and never both in the same bit.
	while( ((_desired_join_state != _join_state) || (_processing)) && (result != MULTI_SOS_IN_PROGRESS) ) {

		// First see if I'm in the middle of processing a state
		if (_processing) {
			result = _process_state();

		} else if (_desired_join_state != _join_state) {
			// if not, then I need to 
			result = _enter_state();
			if (result == MULTI_SOS_IN_PROGRESS) {
				result = _process_state();
			}

		}

		// Finally, see if it's done
		if (result != MULTI_SOS_IN_PROGRESS) {
			// _processing will always be true here, because I either entered 
			// a state above, or was processing
			V_ASSERT(_processing == true);
			_exit_state(result);
		}
	}

	return result;
}

void multi_session_op_join::exit( multi_session_op_status status )
{
	multi_session_platform_info *platform = &(_session->_info.platform);

	switch (status) {
		case MULTI_SOS_SUCCESS:
			// Success - yay, everything should have been handled by the above
			_session->post_external_data();
			break;
			
		case MULTI_SOS_FAILED:
			// Will cause a delete to fire after this finishes - which will call the Close Handle part
			if (_session->_desired_state > MULTI_SS_ALLOCATED) {
				_session->_desired_state = MULTI_SS_ALLOCATED;

			} 
			
			if ((_session->_state < MULTI_SS_ALIVE) && (platform->is_valid() == true)) {
				#if defined(__LIVE_ENABLED)
					// Didn't get far enough to finish a close, so bugger it and continue on
					CloseHandle( platform->handle );
					platform->handle = INVALID_HANDLE_VALUE;

				#elif defined(__PS3)
					platform->invalidate();

				#elif defined(__PS4)
					platform->invalidate();

				#elif defined(__XBOX3)	// HVS_JRS 4/8/2014 xbox3 networking
					platform->invalidate();
				#elif defined(__PC_GP)
					platform->invalidate();

				#endif
			}
			

			break;

		default:
			break;
	}

	#if defined(__NX__)
		if (nex_data != NULL) {
			Nex_data_pool.free(nex_data);
			nex_data = NULL;
		}
	#endif
}

multi_session_op_status multi_session_op_join::process_for_zombie()
{
	V_ERROR_MSG( "You can not zombie JOIN a session.  No one loves zombies." );
	return MULTI_SOS_FAILED;
}


void multi_session_op_join::cancel()
{
	#if defined(__LIVE_ENABLED)
		// If we're int he middle of a create, cancel it, otherwise, we'll have to push a destroy on during the fail
		if (_session->_state < MULTI_SS_ALIVE) {
			XCancelOverlapped( &_overlapped );
			memset( &_overlapped, 0, sizeof(XOVERLAPPED) );

			if (_session->_info.platform.handle != INVALID_HANDLE_VALUE) {
				// Didn't get far enough to finish a close, so bugger it and continue on
				CloseHandle( _session->_info.platform.handle );
				_session->_info.platform.handle = INVALID_HANDLE_VALUE;
			}
		}
	#elif defined(__NX__)
		nex_state = NEX_JOIN_STATE_NONE;
		if (nex_data != NULL) {
			nex_data->ctx.Cancel();
			nex_data->ctx.Reset();
			nex_data->p_ctx.Cancel();
			nex_data->p_ctx.Reset();
		}
	#endif

	// PS3 doesn't need to do anything.
}
