#include "multi_session_op_create.h"

#if defined(__STEAM)
#include "steam_api.h"
#endif

#if defined(__PS3) || defined(__NX__) || defined(__PC_GP)
#include "multi/multi_core/multi_core_matchmaking.h"
#elif defined(__PS4)	// HVS_JRS 2/3/2014 enable ps4 networking
#include "multi/multi_core/multi_core_matchmaking.h"
#elif defined(__XBOX3)	// HVS_JRS 3/26/2014 xbox3 networking
#include "vlib/durango_clr/vlive_clr.hpp"
#endif // __PS3

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

#include "downloadable_content/dlc_game.h"

#include "multi/multi_core/multi_core_platform_pc_drm.h"

// --------------------
//
// Defines
//
// --------------------

//#define	DEBUG_CREATE		// HVS_JRS 2/6/2014 enable debug_spew for create start, process, and exit

// --------------------
//
// Classes/Structures
//
// --------------------

#if defined(__NX__)
struct nex_create_data {
	nn::nex::CallContext ctx;
	nn::nex::ProtocolCallContext p_ctx;
	nn::nex::MatchmakeSession session;
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

static uint64 Session_id_counter = 0;
static bool m_SessionCreateFailed = false;

#if defined(__NX__)
	static object_pool<nex_create_data, MAX_SESSION_OPS> Nex_data_pool;
#endif

#if defined(__PC_GP)
	static bool Live_user_added = false;
	static bool Live_host_changed = false;
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

// --------------------
// 
// External Functions
//
// --------------------

// HVS_DMK
#if defined(__PS4)
extern volatile bool Multi_matchmake_Done;
#endif

void multi_session_op_create::init( multi_session_options const &src, uint8 index )
{
	options.copy( src );
	desired_index = index;
}

bool multi_session_op_create::can_start()
{
	return (_session->_state == MULTI_SS_ALLOCATED);
}

#if defined(__LIVE_ENABLED)

	multi_session_op_status multi_session_op_create::start()
	{
		memset( &_overlapped, 0, sizeof( _overlapped ) );

		// Save off the platform info, I'll be using it alot in this function
		multi_session_platform_info *platform = &(_session->_info.platform);
		platform->invalidate();

		// Return a failure if we are not allowed a coop game
		if (!dlc_game_allow_coop()) {
			return MULTI_SOS_FAILED;
		}

		// Systemlink/LAN games are special - not technically a "session", per se, but just an agreement between '360s to talk to each other.
		// If we aren't connected to LIVE on startup drop to a system link game!
		if (options.systemlink_or_lan == true || multi_core_platform_is_connected_to_internet() == false) {

			options.systemlink_or_lan = true;

			DWORD result = XNetGetTitleXnAddr( &(platform->info.hostAddress) );
			if (result & (XNET_GET_XNADDR_NONE | XNET_GET_XNADDR_PENDING | XNET_GET_XNADDR_TROUBLESHOOT)) {
				// V_ASSERT_MSG(false, "This machine doesn't have a network address yet.");
				// will return failure at the end
			} else {

				INT iresult = XNetCreateKey( &(platform->info.sessionID), &(platform->info.keyExchangeKey) );
				if (iresult == 0) {
					debug_print_system_link_type(platform->info.sessionID);
					_session->debug_spew( "Registering Key." );
					result = XNetRegisterKey( &(platform->info.sessionID), &(platform->info.keyExchangeKey) );
					if (result == 0) {
						return post_create();

					} else {
						V_ASSERT_MSG(false, "Failed to register security exchange key for systemlink/LAN game");
					}
				} else {
					V_ASSERT_MSG(false, "Failed to create security exchange key for systemlink/LAN game.");
				}
			}

		} else { // not systemlink

			// Build creation flags
					
// 			// TODO: Set the user's searchable privacy property to private while we load.
// 			uint private_game = 0;
// 			multi_core_platform_set_user_property(PROPERTY_TEMP_PROP, sizeof(uint), &private_game);

			// NOTE: We probably have other contexts and properties we want here too!
			

			// Make sure the out pointer is clean
			platform->handle = INVALID_HANDLE_VALUE;

			DWORD option_flags = XSESSION_CREATE_HOST | options.live_create_flags();
			DWORD result = XSessionCreate( option_flags, 
				multi_core_platform_get_active_controller(), 
				options.public_slots(),
				options.private_slots(),
				&(platform->nonce), 
				&(platform->info), 
				&_overlapped, 
				&(platform->handle) );

			debug_spew("XSessionCreate host(%x, %d, %d, %d) = %x", option_flags, multi_core_platform_get_active_controller(),
				options.public_slots(), options.private_slots());

			switch(result) {
				case ERROR_SUCCESS:
					return post_create();

				case ERROR_IO_PENDING:
					return MULTI_SOS_IN_PROGRESS;

				default:
					V_ASSERT_MSG(false, "XSessionCreate() appears to have failed!");
					break;
			}
		}

		platform->invalidate();
		return MULTI_SOS_FAILED;

	}

	multi_session_op_status multi_session_op_create::process()
	{
		DWORD op_result;
		DWORD result = XGetOverlappedResult( &_overlapped, &op_result, false );

		multi_session_op_status sos = MULTI_SOS_IN_PROGRESS;

		switch(result) {
			case ERROR_SUCCESS:
				V_ASSERT( op_result == ERROR_SUCCESS );
				memset( &_overlapped, 0, sizeof(XOVERLAPPED) );

				// Alright, see if I can get the proper addresses for my connections
				sos = post_create();
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
	}

	void multi_session_op_create::exit( multi_session_op_status status )
	{
		multi_session_platform_info *platform = &(_session->_info.platform);

		switch (status) {
			case MULTI_SOS_SUCCESS:
				V_ASSERT( _session->get_system()->get_my_state() == MULTI_SSS_JOINED );
				_session->post_external_data(true);
				break;
				
			case MULTI_SOS_FAILED:
				// Will cause a delete to fire after this finishes - which will call the Close Handle part
				if (_session->_desired_state > MULTI_SS_ALLOCATED) {
					_session->_desired_state = MULTI_SS_ALLOCATED;

				} 
				
				// If we didn't get far enough where the session will clean this up, clean it up ourselves.
				if ((platform->handle != INVALID_HANDLE_VALUE) && (_session->_state < MULTI_SS_ALIVE)) {
					CloseHandle( platform->handle );
					platform->handle = INVALID_HANDLE_VALUE;
				}

				break;

			default:
				break;
		}
	}

#elif defined(__PS3)

	multi_session_op_status multi_session_op_create::start()
	{
		memset(&m_current_cb_event, 0, sizeof(m_current_cb_event));

		if (multi_core_platform_is_connected_to_network() == false) {
			return MULTI_SOS_FAILED;	// Can do nothing with a non-connected session
		}

		// Return a failure if we are not allowed a coop game
		if (!dlc_game_allow_coop()) {
			return MULTI_SOS_FAILED;
		}

		if (_session->_reviving) {
			// Reinitialize matchmaking.
			multi_core_matchmaking_system_internal_init(NULL);
		}

		SceNpSignalingNetInfo host_net_info;
		host_net_info.size = sizeof(SceNpSignalingNetInfo);
		int32 ret = sceNpSignalingGetLocalNetInfo(0, &host_net_info);

		if (ret >= 0) {
			_session->_info.platform.create_local_session_id( _session );
			if ( _session->_info.platform.is_valid() ){
				// Systemlink/LAN games are special - not technically a "session", 
				// per se, but just an agreement between PS3's to talk to each other.
				// If we are not signed in to PSN or we are restricted from online games, drop to a syslink game.
				if (options.systemlink_or_lan == true || multi_core_platform_is_connected_to_internet() == false ||
					multi_core_platform_user_has_privilege(MULTI_USER_PRIVILEGE_ONLINE_SESSIONS) == false) {

					// For now, if we aren't connected to PSN on startup drop to a system link game!
					// We may want to re-evaluate this later but for now it will handle signed out players much better.
					options.systemlink_or_lan = true;

					m_current_cb_event.complete = true;
					m_current_cb_event.error = 0;
				} else {
					// Flag that we haven't started creating a room yet.
					m_started_room_create = false;
				}

				if ( post_create() == MULTI_SOS_FAILED ) {
					return MULTI_SOS_FAILED;
				}

				return MULTI_SOS_IN_PROGRESS;

			} else {
				return MULTI_SOS_FAILED;
			}
			
		} else {
			debug_spew( !"Probably not connected to the internet." );
			return MULTI_SOS_FAILED;
		}
	}

	multi_session_op_status multi_session_op_create::process()
	{
		// Systemlink/LAN games are special - not technically a "session", 
		// per se, but just an agreement between PS3's to talk to each other.
		if (options.systemlink_or_lan == true) {
			return MULTI_SOS_SUCCESS;
		}

		// Wait for world ID to resolve.
		if (NP_world_search_completed == false) {
			return MULTI_SOS_IN_PROGRESS;
		}

		// Something went wrong getting world ID, we can't create a room without it.
		if (Np_world_id == PS3_INVALID_WORLD_ID) {
			return MULTI_SOS_FAILED;
		}

		// Start creating the room if we haven't yet.
		if (m_started_room_create == false) {
			m_current_cb_event.complete = true;
			m_current_cb_event.opt = &(_session->_info.platform.session_id);

			SceNpMatching2IntAttr	properties[SCE_NP_MATCHING2_ROOM_SEARCHABLE_INT_ATTR_EXTERNAL_NUM];
			int							num_properties;
			SceNpMatching2BinAttr	bin_properties[SCE_NP_MATCHING2_ROOM_BIN_ATTR_EXTERNAL_NUM];
			int							num_bin_properties;
			multi_ps3_get_session_properties(properties, &num_properties, bin_properties, &num_bin_properties, &options);

			m_started_room_create = true;

			bool passed = multi_ps3_np_create_room(&(m_current_cb_event), options.max_players, properties, num_properties, bin_properties, num_bin_properties);
			if (!passed) {
				V_ASSERT_MSG(false, "multi_ps3_np_create_room failed\n");

				// NOTE: We could warn the player and drop to a symlink game at this point!

				return MULTI_SOS_FAILED;
			}

		}

		// The callback has not finished yet.
		if (!m_current_cb_event.complete) {
			return MULTI_SOS_IN_PROGRESS;
		}

		if (m_current_cb_event.error < 0) {
			debug_spew("multi_session_op_create - create room failed error code 0x%x\n", m_current_cb_event.error);

			// NOTE: We could warn the player and drop to a symlink game at this point!

			return MULTI_SOS_FAILED;
		}

		return MULTI_SOS_SUCCESS;
	}

	void multi_session_op_create::exit( multi_session_op_status status )
	{
		switch (status) {
			case MULTI_SOS_SUCCESS:
				V_ASSERT( _session->get_system()->get_my_state() == MULTI_SSS_JOINED );
				_session->post_external_data(true);
				break;

			case MULTI_SOS_FAILED:
				// Will cause a delete to fire after this finishes - which will call the Close Handle part
				if (_session->_desired_state > MULTI_SS_ALLOCATED) {
					_session->_desired_state = MULTI_SS_ALLOCATED;

				} 

				// Delete room if needed!
				if (m_current_cb_event.complete && m_current_cb_event.error >= 0) {
					multi_ps3_np_delete_room(NULL, _session->_info.platform.session_id);
				}

				break;

			default:
				break;
		}
	}

#elif defined(__PS4)	// HVS_JRS 2/3/2014 enable ps4 networking

	multi_session_op_status multi_session_op_create::start()
	{
		memset(&m_current_cb_event, 0, sizeof(m_current_cb_event));

		if (multi_core_platform_is_connected_to_network() == false) {
#if defined(DEBUG_CREATE)
			debug_spew( "create start 1");
#endif
			return MULTI_SOS_FAILED;	// Can do nothing with a non-connected session
		}

		// Return a failure if we are not allowed a coop game
		if (!dlc_game_allow_coop()) {
#if defined(DEBUG_CREATE)
			debug_spew( "create start 2");
#endif
			return MULTI_SOS_FAILED;
		}

		if (_session->_reviving) {
#if defined(DEBUG_CREATE)
			debug_spew( "create start 3");
#endif
			// Reinitialize matchmaking.
			multi_core_matchmaking_system_internal_init(NULL);//HVS_DMK
			//multi_core_matchmaking_system_init(NULL);//HVS_DMK

			debug_spew("POST multi_core_matchmaking_system_init()\n");//HVS_DMK
#if defined(__PS4)
			while (Multi_matchmake_Done == false)
			{
				debug_spew("waiting for multi_core_matchmaking_system_init\n");
				os_sleep(10);
			}
#endif
		}
		else
		{
			Multi_matchmake_Done = true;
		}

		SceNpSignalingNetInfo host_net_info;
		host_net_info.size = sizeof(SceNpSignalingNetInfo);
		int32 ret = sceNpSignalingGetLocalNetInfo(0, &host_net_info);

		if (ret >= 0) {
#if defined(DEBUG_CREATE)
			debug_spew( "create start 4");
#endif
			_session->_info.platform.create_local_session_id( _session );
			if ( _session->_info.platform.is_valid() ){
#if defined(DEBUG_CREATE)
				debug_spew( "create start 5");
#endif
				// Systemlink/LAN games are special - not technically a "session", 
				// per se, but just an agreement between PS4's to talk to each other.
				// If we are not signed in to PSN or we are restricted from online games, drop to a syslink game.
				if (options.systemlink_or_lan == true || multi_core_platform_is_connected_to_internet() == false ||
					Matchmaking_context_failed || 		// HVS_JRS 7/10/2014 if matchmaking init failed, treat as offline
					multi_core_platform_user_has_privilege(MULTI_USER_PRIVILEGE_ONLINE_SESSIONS) == false) {

#if defined(DEBUG_CREATE)
					debug_spew( "create start 6");
#endif
					// For now, if we aren't connected to PSN on startup drop to a system link game!
					// We may want to re-evaluate this later but for now it will handle signed out players much better.
					options.systemlink_or_lan = true;

					m_current_cb_event.complete = true;
					m_current_cb_event.error = 0;
				} else {
#if defined(DEBUG_CREATE)
					debug_spew( "create start 7");
#endif
					// Flag that we haven't started creating a room yet.
					m_started_room_create = false;
				}

				if ( post_create() == MULTI_SOS_FAILED ) {
#if defined(DEBUG_CREATE)
					debug_spew( "create start 8");
#endif
					return MULTI_SOS_FAILED;
				}

#if defined(DEBUG_CREATE)
				debug_spew( "create start 9");
#endif
				return MULTI_SOS_IN_PROGRESS;

			} else {
#if defined(DEBUG_CREATE)
				debug_spew( "create start 10");
#endif
				return MULTI_SOS_FAILED;
			}
			
		} else {
#if defined(DEBUG_CREATE)
			debug_spew( "create start 11");
#endif
			debug_spew( "Probably not connected to the internet." );
			return MULTI_SOS_FAILED;
		}
#if defined(DEBUG_CREATE)
		debug_spew( "create start 12");
#endif
	}

	multi_session_op_status multi_session_op_create::process()
	{
		// Systemlink/LAN games are special - not technically a "session", 
		// per se, but just an agreement between PS4's to talk to each other.
		if (options.systemlink_or_lan == true) {
#if defined(DEBUG_CREATE)
			debug_spew( "create process 1");
#endif
			return MULTI_SOS_SUCCESS;
		}

		if (Matchmaking_context_failed)	// HVS_JRS 2/6/2014 added check for failure
		{
#if defined(DEBUG_CREATE)
			debug_spew( "create process 2");
#endif
			return MULTI_SOS_FAILED;
		}

		if (Multi_matchmake_Done == false)
		{
#if defined(DEBUG_CREATE)
			debug_spew( "create process 2a");
#endif
			return MULTI_SOS_IN_PROGRESS;
		}

		// Wait for world ID to resolve.
		if (NP_world_search_completed == false) {
#if defined(DEBUG_CREATE)
			debug_spew( "create process 3");
#endif
			return MULTI_SOS_IN_PROGRESS;
		}

		// Something went wrong getting world ID, we can't create a room without it.
		if (Np_world_id == PS4_INVALID_WORLD_ID) {
#if defined(_DEBUG)
			debug_spew( "create process 4");
#endif
			return MULTI_SOS_FAILED;
		}

		// Start creating the room if we haven't yet.
		if (m_started_room_create == false) {
#if defined(DEBUG_CREATE)
			debug_spew( "create process 5");
#endif
			m_current_cb_event.complete = true;
			m_current_cb_event.opt = &(_session->_info.platform.session_id);

			SceNpMatching2IntAttr	properties[SCE_NP_MATCHING2_ROOM_SEARCHABLE_INT_ATTR_EXTERNAL_NUM];
			int							num_properties;
			SceNpMatching2BinAttr	bin_properties[SCE_NP_MATCHING2_ROOM_BIN_ATTR_EXTERNAL_NUM];
			int							num_bin_properties;
			multi_ps4_get_session_properties(properties, &num_properties, bin_properties, &num_bin_properties, &options);

			m_started_room_create = true;

			bool passed = multi_ps4_np_create_room(&(m_current_cb_event), options.max_players, properties, num_properties, bin_properties, num_bin_properties);
			if (!passed) {
#if defined(DEBUG_CREATE)
				debug_spew( "create process 6");
#endif
				V_ASSERT_MSG(false, "multi_ps4_np_create_room failed\n");

				// NOTE: We could warn the player and drop to a symlink game at this point!

				return MULTI_SOS_FAILED;
			}

		}

		// The callback has not finished yet.
		if (!m_current_cb_event.complete) {
#if defined(DEBUG_CREATE)
			debug_spew( "create process 7");
#endif
			return MULTI_SOS_IN_PROGRESS;
		}

		if (m_current_cb_event.error < 0) {
#if defined(DEBUG_CREATE)
			debug_spew( "create process 8");
#endif
			debug_spew("multi_session_op_create - create room failed error code 0x%x\n", m_current_cb_event.error);

			// NOTE: We could warn the player and drop to a symlink game at this point!

			return MULTI_SOS_FAILED;
		}

#if defined(DEBUG_CREATE)
		debug_spew( "create process 9");
#endif
		return MULTI_SOS_SUCCESS;
	}

	void multi_session_op_create::exit( multi_session_op_status status )
	{
#if defined(DEBUG_CREATE)
		debug_spew( "create exit 1 status:%d", status);
#endif
		switch (status) {
			case MULTI_SOS_SUCCESS:
#if defined(DEBUG_CREATE)
				debug_spew( "create exit 2");
#endif
				V_ASSERT( _session->get_system()->get_my_state() == MULTI_SSS_JOINED );
				_session->post_external_data(true);
				break;

			case MULTI_SOS_FAILED:
#if defined(DEBUG_CREATE)
				debug_spew( "create exit 3");
#endif
				// Will cause a delete to fire after this finishes - which will call the Close Handle part
				if (_session->_desired_state > MULTI_SS_ALLOCATED) {
#if defined(DEBUG_CREATE)
					debug_spew( "create exit 4");
#endif
					_session->_desired_state = MULTI_SS_ALLOCATED;

				} 

				// Delete room if needed!
				if (m_current_cb_event.complete && m_current_cb_event.error >= 0) {
#if defined(DEBUG_CREATE)
					debug_spew( "create exit 5");
#endif
					multi_ps4_np_delete_room(NULL, _session->_info.platform.session_id);
				}

				break;

			default:
#if defined(DEBUG_CREATE)
				debug_spew( "create exit 6");
#endif
				break;
		}
#if defined(DEBUG_CREATE)
		debug_spew( "create exit 7");
#endif
	}

#elif defined(__STEAM)

	multi_session_op_status multi_session_op_create::start()
	{
		// Save off the platform info, I'll be using it alot in this function
		multi_session_platform_info *platform = &(_session->_info.platform);
		platform->invalidate();

		// Systemlink/LAN games are special - not technically a "session", per se.
		if (options.systemlink_or_lan == true || multi_core_platform_is_connected_to_internet() == false) {
			// If this wasn't LAN before, it is now.
			options.systemlink_or_lan = true;

			connection_address local_address;
			local_address.init();
			multi_platform_get_local_ip_address(local_address);

			// Because we're silently switching to LAN, blast away our old address for a shiny new one.
			_session->_info.host.addr.copy(local_address);

			this->m_desired_info.m_host_ip_address = local_address.addr.address;
			this->m_desired_info.m_host_steam_id64 = SteamUser() != NULL ? SteamUser()->GetSteamID().ConvertToUint64() : 0;
			this->m_desired_info.m_host_session_id64 = Session_id_counter;
			++Session_id_counter;

			return post_create();

		} else { // not systemlink

#if !defined(__RELEASE_FINAL)
			// Handle running the game without Steam.
			if(SteamMatchmaking() == NULL) {
				return post_create();
			}
#else
			V_ASSERT_RETURN_VALUE(SteamMatchmaking() != NULL, MULTI_SOS_FAILED);
#endif

			// Build creation flags
			ELobbyType lobby_type;
			int num_slots;
			if(options.public_slots() > 0) {
				V_ASSERT(options.private_slots() == 0); // Steam only supports fully public or fully private lobbies.
				num_slots = options.public_slots();
				lobby_type = k_ELobbyTypePublic;
			} else {
				V_ASSERT(options.public_slots() == 0);
				num_slots = options.private_slots();
				lobby_type = k_ELobbyTypeFriendsOnly;
			}

			// Create the lobby
			m_lobby_creation_failed = false;
			m_desired_info.m_host_session_id64 = k_uAPICallInvalid;
			SteamAPICall_t result = SteamMatchmaking()->CreateLobby(lobby_type, num_slots);
			m_lobby_created_result.Set(result, this, &multi_session_op_create::event_lobby_created);

			switch(result) {
					case k_uAPICallInvalid:
						V_ASSERT_MSG(false, "Couldn't create Steam lobby.");
						break;
					default:
						if(post_create() == MULTI_SOS_FAILED) {
							return MULTI_SOS_FAILED;
						}
						return MULTI_SOS_IN_PROGRESS;
			}
		}

		platform->invalidate();
		return MULTI_SOS_FAILED;
	}

	DRM_NO_INLINE void multi_session_op_create::event_lobby_created(LobbyCreated_t *pCallback, bool bIOFailure)
	{
		CEG_PROTECT_MEMBER_FUNCTION_BATCH_2(multi_session_op_create__event_lobby_created);

		// Make sure we succeeded.
		if(pCallback->m_eResult != k_EResultOK) {
			m_lobby_creation_failed = true;
			return;
		}

		// Remember the lobby id and set the data.
		m_desired_info.m_host_session_id64 = pCallback->m_ulSteamIDLobby;
		CSteamID lobby_id;
		lobby_id.SetFromUint64(m_desired_info.m_host_session_id64);
		m_desired_info.m_host_steam_id64 = SteamMatchmaking()->GetLobbyOwner(lobby_id).ConvertToUint64();
		SteamMatchmaking()->SetLobbyData(lobby_id, "in_game", "false");
		SteamMatchmaking()->SetLobbyData(lobby_id, "game_mode", "campaign");
#if defined(__LOW_VIOLENCE_DE_BUILD)
		SteamMatchmaking()->SetLobbyData(lobby_id, "lv", "true");
#else
		SteamMatchmaking()->SetLobbyData(lobby_id, "lv", "false");
#endif
	}

	DRM_NO_INLINE multi_session_op_status multi_session_op_create::process()
	{
		CEG_PROTECT_MEMBER_FUNCTION_BATCH_2(multi_session_op_create__process);

		// If we failed, bail.
		if(m_lobby_creation_failed) {
			return MULTI_SOS_FAILED;
		}

		// Wait for the Steam callback.
		if(m_desired_info.m_host_session_id64 == k_uAPICallInvalid) {
			return MULTI_SOS_IN_PROGRESS;
		}
		return MULTI_SOS_SUCCESS;
	}

	void multi_session_op_create::exit( multi_session_op_status status )
	{
		switch (status) {
				case MULTI_SOS_SUCCESS:
					V_ASSERT( _session->get_system() != NULL );
					V_ASSERT( _session->get_system()->get_my_state() == MULTI_SSS_JOINED );
					_session->_info.platform.copy(m_desired_info);
					_session->post_external_data(true);
					break;

				case MULTI_SOS_FAILED:
					// Will cause a delete to fire after this finishes - which will call the Close Handle part
					if (_session->_desired_state > MULTI_SS_ALLOCATED) {
						_session->_desired_state = MULTI_SS_ALLOCATED;
					} 

					// If the lobby was created, we should nuke it.
					if(SteamMatchmaking() != NULL && m_desired_info.m_host_session_id64 != k_uAPICallInvalid) {
						SteamMatchmaking()->LeaveLobby(m_desired_info.m_host_session_id64);
					}
					break;

				default:
					break;
		}
	}
#elif defined(__XBOX3) // HVS_MMK[PRINCE] 01/17/2014 :
	multi_session_op_status multi_session_op_create::start()
	{
		//HVS_ASL[PRINCE] 09-02-2014: Changed this to be asynchronous so the user does not get impacted when creating a new session during gameplay.
		LiveCLR::SetIsHost(true);				// HVS_JRS 4/28/2014 tell LiveCLR we are the host

		debug_spew("multi_session_op_create::start called");	// HVS_JRS 3/25/2014 xbox3 networking debug information
		m_SessionCreateFailed = false;

		multi_session_platform_info *platform = &(_session->_info.platform);
		platform->invalidate();

		//HVS_ASL[PRINCE] 09-03-2014: Use the Async version
		LiveCLR::AsyncStatus createStatus = LiveCLR::CreateMatchmakingTicketAsync(true, (int)_session->_info.options.social_type, false);
		
		// Return the current status (should be in progress or failed).
		return 	GetSOSFromLiveCLRAsyncStatus(createStatus);
	}

	multi_session_op_status multi_session_op_create::process()
	{
		//HVS_ASL[PRINCE] 09-02-2014: Changed this to be asynchronous so the user does not get impacted when creating a new session during gameplay.

		// Get the current status
		multi_session_op_status retSOS = GetSOSFromLiveCLRAsyncStatus(LiveCLR::GetCreateMatchmakingTicketAsyncStatus());

		switch (retSOS)
		{
		case MULTI_SOS_SUCCESS:
			{
				//HVS_ASL[PRINCE] 09-02-2014: Do the work that was after the CreateMatchmakingTicket call succeeded in start()
				//HVS_ASL[PRINCE] 09-03-2014: The CreateGameSession process already does this.  LiveCLR::SetJoinRestriction((LiveCLR::JoinRestriction)MULTI_SST_PRIVATE);	// HVS_JRS 6/26/2014 the modify operation will set once we are joinable (post loading)
				// HVS_JRS 4/8/2014 get game session id
				multi_session_platform_info *platform = &(_session->_info.platform);

				LPCGUID sessionptr = LiveCLR::GetGameSessionGUID();
				if (sessionptr)
				{
					memcpy((void *)&platform->session_id, (void *)sessionptr, sizeof(GUID));
				}

				debug_spew("LiveCLR::CreateGameSession SUCCESS");
				retSOS = post_create();
			}
			break;
		case MULTI_SOS_FAILED:
			m_SessionCreateFailed = true;
			_session->_desired_state = _session->_state;	//avoid calling _host() again
			debug_spew("LiveCLR::CreateGameSession FAILED");
		case MULTI_SOS_IN_PROGRESS:
		default:
			break;
		}
		return retSOS;
	}

	extern void multi_core_platform_internal_restore_game_presence_context();

	void multi_session_op_create::exit( multi_session_op_status status )
	{
		//HVS_ASL[PRINCE] 10-15-2014: If reviving then OP_MODIFY may not be used so we need to make sure the join restriction is updated here.
		debug_spew("multi_session_op_create::exit: _session->_reviving = %s",(_session->_reviving) ? "TRUE" : "FALSE");
		if(_session->_reviving)
		{
			debug_spew("multi_session_op_create::exit: Setting Join Restriction to %d",_session->_info.options.social_type);
			LiveCLR::SetJoinRestriction((LiveCLR::JoinRestriction)_session->_info.options.social_type);
			multi_core_platform_internal_restore_game_presence_context();
		}
	}
#elif defined(__NX__)

multi_session_op_status multi_session_op_create::nex_create_start()
{
	V_ASSERT_RETURN_VALUE(nex_data->p_ctx.GetState() == nn::nex::CallContext::Available, MULTI_SOS_FAILED);

	// NX has no invites
	V_ASSERT(options.social_type == MULTI_SST_PUBLIC || options.social_type == MULTI_SST_FRIENDS_ONLY);

	nn::nex::MatchmakeSession* auto_matchmaking_session = multi_core_nx64_get_auto_matchmaking_session();
	if (auto_matchmaking_session != NULL) {
		nex_data->session = *auto_matchmaking_session;
		options.social_type = MULTI_SST_PUBLIC;
		// Do not create a new session, we have already a hosting auto matchmaking session
		_session->_info.platform.gathering_id = auto_matchmaking_session->GetID();
		nn::nex::Network::GetInstance()->SetP2PDataPacketSessionSignatureKey(auto_matchmaking_session->GetSessionKey());
		nn::nex::Stream::GetSettings(nn::nex::Stream::Game).SetIsEncryptionRequired(true);
		nn::nex::Stream::GetSettings(nn::nex::Stream::Game).SetEncryptionKey(auto_matchmaking_session->GetSessionKey());
		return nex_nat_start();
	}

	nex_data->session.SetMinParticipants(1);
	nex_data->session.SetMaxParticipants(options.max_players);
	nex_data->session.SetGameMode(MATCHMAKE_GAME_MODE);
	nex_data->session.SetMatchmakeSystemType(nn::nex::MATCHMAKE_SYSTEM_TYPE_ANYBODY); // This is the only value that makes sense (not checking options.social_type)
	nex_data->session.SetAttribute(MATCHMAKE_ATTRIBUTE_GAME_MODE, (uint32)options.game_mode);
	nex_data->session.SetAttribute(MATCHMAKE_ATTRIBUTE_PRIVACY, (uint32)options.social_type);
	nex_data->session.SetOpenParticipation(options.closed == false);
	nn::nex::CreateMatchmakeSessionParam nex_session_param;
	nex_session_param.SetSourceMatchmakeSession(nex_data->session);

	if (!Matchmaking_client->CreateMatchmakeSession(&nex_data->p_ctx, nex_session_param, &nex_data->session)) {
		V_ASSERT_RELEASE(false); // should never happen
		return MULTI_SOS_FAILED;
	}
	nex_state = NEX_CREATE_STATE_CREATING;
	return MULTI_SOS_IN_PROGRESS;
}

multi_session_op_status multi_session_op_create::nex_create_process()
{
	switch (nex_data->p_ctx.GetState()) {
	case nn::nex::CallContext::CallInProgress:
		return MULTI_SOS_IN_PROGRESS;
	case nn::nex::CallContext::CallFailure:
	case nn::nex::CallContext::CallCancelled:
		handle_nex_error("nn::nex::MatchmakeExtensionClient::CreateMatchmakeSession()", nex_data->p_ctx.GetOutcome());
		nex_data->p_ctx.Reset();
		return MULTI_SOS_FAILED;
	case nn::nex::CallContext::CallSuccess:
		nex_data->p_ctx.Reset();
		_session->_info.platform.gathering_id = nex_data->session.GetID();
		nn::nex::Network::GetInstance()->SetP2PDataPacketSessionSignatureKey(nex_data->session.GetSessionKey());
		nn::nex::Stream::GetSettings(nn::nex::Stream::Game).SetIsEncryptionRequired(true);
		nn::nex::Stream::GetSettings(nn::nex::Stream::Game).SetEncryptionKey(nex_data->session.GetSessionKey());
		return nex_nat_start();
	default:
		V_ASSERT_MSG(false, "Invalid state");
	}
	return MULTI_SOS_FAILED;
}

multi_session_op_status multi_session_op_create::nex_nat_start()
{
	V_ASSERT_RETURN_VALUE(nex_data->ctx.GetState() == nn::nex::CallContext::Available, MULTI_SOS_FAILED);

	if (!nn::nex::ConnectivityManager::GetInstance()->StartNATSession(&nex_data->ctx)) {
		V_ASSERT_RELEASE(false); // should never happen
		return MULTI_SOS_FAILED;
	}
	nex_state = NEX_CREATE_STATE_START_NAT;
	return MULTI_SOS_IN_PROGRESS;
}

multi_session_op_status multi_session_op_create::nex_nat_process()
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

multi_session_op_status multi_session_op_create::nex_voice_chat_start()
{
	V_ASSERT_RETURN_VALUE(nex_data->p_ctx.GetState() == nn::nex::CallContext::Available, MULTI_SOS_FAILED);
	    
	nn::nex::SmartDeviceVoiceChatJoinRoomParam param;
    param.SetSessionId(_session->_info.platform.gathering_id);
    param.SetGameMode(MATCHMAKE_GAME_MODE);
	if (!Smart_device_voice_chat_client->JoinRoom(&nex_data->p_ctx, param, &nex_data->join_room_result)) {
		V_ASSERT_RELEASE(false); // should never happen
		return MULTI_SOS_FAILED;
	}
	nex_state = NEX_CREATE_STATE_VOICE_JOIN;
	return MULTI_SOS_IN_PROGRESS;	
}

multi_session_op_status multi_session_op_create::nex_voice_chat_process()
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

multi_session_op_status multi_session_op_create::nex_p2p_start()
{
	if (!nex_p2p_socket_open()) {
		if (_session->_info.platform.voice_chat_room_id != 0) {
			nn::nex::SmartDeviceVoiceChatLeaveRoomParam param;
			param.SetRoomId(_session->_info.platform.voice_chat_room_id);
			Smart_device_voice_chat_client->LeaveRoom(param);
		}
		nn::nex::ConnectivityManager::GetInstance()->StopNATSession();
		multi_core_nx64_start_logout(); // Logout to clean up sessions
		return MULTI_SOS_FAILED;
	}
	nex_state = NEX_CREATE_STATE_NONE;
	return post_create();
}

void multi_session_op_create::handle_nex_error(const char* func_name, const nn::nex::qResult& result)
{
	if (nex_state >= NEX_CREATE_STATE_START_NAT) {
		nn::nex::ConnectivityManager::GetInstance()->StopNATSession();
	}
	multi_session_operation::handle_nex_error(func_name, result);
	multi_core_nx64_start_logout(); // Logout to clean up sessions
}

multi_session_op_status multi_session_op_create::start()
{
	_session->_info.platform.invalidate();

	// Return a failure if we are not allowed a coop game
	if (!dlc_game_allow_coop()) {
		return MULTI_SOS_FAILED;
	}

	// Systemlink/LAN games are special - not technically a "session", per se, but just an agreement between '360s to talk to each other.
	// If we aren't connected to LIVE on startup drop to a system link game!
	if (options.systemlink_or_lan == true) {
		connection_address local_address;
		multi_platform_get_local_ip_address(local_address);
		_session->_info.platform.host_ip_address = local_address.addr.address;
		_session->_info.platform.gathering_id = Session_id_counter;
		++Session_id_counter;
		return post_create();
	} 
	else if (multi_core_platform_is_connected_to_internet() == true) {
		nex_data = Nex_data_pool.alloc();
		V_ASSERT(nex_data != NULL);
		if (nex_data == NULL) {
			return MULTI_SOS_FAILED;
		}
		return nex_create_start();
	}

	return MULTI_SOS_FAILED;
}

multi_session_op_status multi_session_op_create::process()
{
	// Systemlink/LAN games are special - not technically a "session", 
	// per se, but just an agreement between PS3's to talk to each other.
	if (options.systemlink_or_lan == true) {
		return MULTI_SOS_SUCCESS;
	} else if (nex_data != NULL) {
		switch (nex_state) {
		case NEX_CREATE_STATE_CREATING:
			return nex_create_process();
		case NEX_CREATE_STATE_START_NAT:
			return nex_nat_process();
		case NEX_CREATE_STATE_VOICE_JOIN:
			return nex_voice_chat_process();
		case NEX_CREATE_STATE_NONE:
		default:
			V_ASSERT_MSG(false, "Invalid state");
			break;
		}		
	}
	return MULTI_SOS_FAILED;
}

void multi_session_op_create::exit(multi_session_op_status status)
{
	switch (status) {
		case MULTI_SOS_SUCCESS:
			V_ASSERT(_session->get_system() != NULL);
			V_ASSERT(_session->get_system()->get_my_state() == MULTI_SSS_JOINED);
			_session->post_external_data(true);
			break;

		case MULTI_SOS_FAILED:
			// Will cause a delete to fire after this finishes - which will call the Close Session part
			if (_session->_desired_state > MULTI_SS_ALLOCATED) {
				_session->_desired_state = MULTI_SS_ALLOCATED;
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

#elif defined(__PC_GP)

multi_session_op_status multi_session_op_create::pcgp_create_start()
{
	V_ASSERT(options.social_type == MULTI_SST_PUBLIC || options.social_type == MULTI_SST_FRIENDS_ONLY);

	bool is_auto_matchmaking = multi_core_pcgp_is_auto_matchmaking();
	if (is_auto_matchmaking) {
		options.social_type = MULTI_SST_PUBLIC;

		_session->_info.platform.session_id = live_get_matchmaking_session_guid();
		
		SLNet::RakNetGUID guid(live_get_matchmaking_raknet_host_guid());
		_session->_info.host.addr.addr.rak_address = guid;

		pcgp_state = PCGP_CREATE_STATE_NONE;
		return post_create();
	}

	Live_user_added = false;
	Live_host_changed = false;

	pcgp_state = PCGP_CREATE_STATE_CREATING;

	return MULTI_SOS_IN_PROGRESS;
}

multi_session_op_status multi_session_op_create::pcgp_create_process()
{
	const XblMultiplayerEvent* current_event = Live_multiplayer_events;
	for (int i = 0; i < Live_multiplayer_events_count; ++i, ++current_event) {
		switch (current_event->EventType)
		{
		case XblMultiplayerEventType::UserAdded:
			if (SUCCEEDED(current_event->Result)) {
				Live_user_added = true;
			}
			else {
				handle_live_error("XblMultiplayerManagerLobbySessionAddLocalUser_UserAdded", current_event->Result);
				return MULTI_SOS_FAILED;
			}
			break;
		case XblMultiplayerEventType::HostChanged:
			if (SUCCEEDED(current_event->Result)) {
				Live_host_changed = true;
			}
			else {
				handle_live_error("XblMultiplayerManagerLobbySessionAddLocalUser_HostChanged", current_event->Result);
				return MULTI_SOS_FAILED;
			}
			break;
		default:
			// Do nothing.
			break;
		}
	}

	if (Live_user_added && Live_host_changed) {
		XblMultiplayerSessionReference sessionRef;
		HRESULT hr = XblMultiplayerManagerLobbySessionSessionReference(&sessionRef);
		if (SUCCEEDED(hr)) {
			GUID session_id;
			CoCreateGuid(&session_id);
			if (!string_to_guid(sessionRef.SessionName, &session_id)) {
				return MULTI_SOS_FAILED;
			}

			multi_session_platform_info *platform = &(_session->_info.platform);
			platform->session_id = session_id;
		}
		else {
			handle_live_error("XblMultiplayerManagerLobbySessionSessionReference", hr);
			return MULTI_SOS_FAILED;
		}
		pcgp_state = PCGP_CREATE_STATE_UDP_SERVER_CONNECTING;
		return pcgp_udp_connection_start();
	}

	return MULTI_SOS_IN_PROGRESS;
}

multi_session_op_status multi_session_op_create::pcgp_udp_connection_start()
{
	// Connect to our UDP server
	if (net_udp_proxy_server_connect() == false) {
		handle_raknet_error("net_udp_proxy_server_connect", net_get_udp_error_code());
		return MULTI_SOS_FAILED;
	}

	return MULTI_SOS_IN_PROGRESS;
}

multi_session_op_status multi_session_op_create::pcgp_udp_connection_process()
{
	udp_server_connection_state state = net_udp_server_connection_state();
	switch (state) {
	case UDP_SERVER_CONNECTION_CONNECTED:
		// for udp proxy and xbox live stuff
		pcgp_state = PCGP_CREATE_STATE_XBL_RAKGUID_WRITING;
		return pcgp_xbl_write_rakguid();
	case UDP_SERVER_CONNECTION_NONE:
	case UDP_SERVER_CONNECTION_FAILED:
		handle_raknet_error("net_udp_server_connection_state", net_get_udp_error_code());
		return MULTI_SOS_FAILED;
	}

	return MULTI_SOS_IN_PROGRESS;
}

multi_session_op_status multi_session_op_create::pcgp_xbl_write_rakguid()
{
	uint64_t my_raknet_guid = net_get_rak_guid();
	if (my_raknet_guid == -1) {
		return MULTI_SOS_FAILED;
	}

	SLNet::RakNetGUID guid(my_raknet_guid);
	_session->_info.host.addr.addr.rak_address = guid;

	return MULTI_SOS_IN_PROGRESS;
}

multi_session_op_status multi_session_op_create::pcgp_xbl_write_rakguid_process()
{
	const XblMultiplayerEvent* current_event = Live_multiplayer_events;
	for (int i = 0; i < Live_multiplayer_events_count; ++i, ++current_event) {
		switch (current_event->EventType)
		{
		case XblMultiplayerEventType::LocalMemberConnectionAddressWriteCompleted:
			if (SUCCEEDED(current_event->Result)) {
				pcgp_state = PCGP_CREATE_STATE_NONE;
				return post_create();
			}
			else {
				handle_live_error("XblMultiplayerManagerLobbySessionSetLocalMemberConnectionAddress_LocalMemberConnectionAddressWriteCompleted", current_event->Result);
				return MULTI_SOS_FAILED;
			}
			break;
		default:
			// Do nothing.
			break;
		}
	}

	return MULTI_SOS_IN_PROGRESS;
}

multi_session_op_status multi_session_op_create::start()
{
	// Save off the platform info, I'll be using it alot in this function
	multi_session_platform_info *platform = &(_session->_info.platform);
	platform->invalidate();

	// Systemlink/LAN games are special - not technically a "session", per se.
	if (options.systemlink_or_lan == true || multi_core_platform_is_connected_to_internet() == false) {
		// If this wasn't LAN before, it is now.
		options.systemlink_or_lan = true;


		net_set_lan_availability(true);

		connection_address local_address;
		local_address.init();
		multi_platform_get_local_ip_address(local_address);

		// Because we're silently switching to LAN, blast away our old address for a shiny new one.
		_session->_info.host.addr.copy(local_address);
		_session->_info.platform.host_ip_address = local_address.addr.address;
		++Session_id_counter;

		return post_create();
	}
	else if (multi_core_platform_is_connected_to_internet()) { // not systemlink
#if defined(FAKE_USER_ENABLED)
		return post_create();
#else
		return pcgp_create_start();
#endif
	}

	return MULTI_SOS_FAILED;
}

multi_session_op_status multi_session_op_create::process()
{
	if (options.systemlink_or_lan == true) {
		return MULTI_SOS_SUCCESS;
	}
	else {
		switch (pcgp_state) {
		case PCGP_CREATE_STATE_CREATING:
			return pcgp_create_process();
		case PCGP_CREATE_STATE_UDP_SERVER_CONNECTING:
			return pcgp_udp_connection_process();
		case PCGP_CREATE_STATE_XBL_RAKGUID_WRITING:
			return pcgp_xbl_write_rakguid_process();
		case PCGP_CREATE_STATE_NONE:
		default:
			V_ASSERT_MSG(false, "Invalid state");
			break;
		}
	}

	return MULTI_SOS_FAILED;
}

void multi_session_op_create::exit(multi_session_op_status status)
{
	Live_user_added = false;
	Live_host_changed = false;

	switch (status) {
	case MULTI_SOS_SUCCESS:
		V_ASSERT(_session->get_system() != NULL);
		V_ASSERT(_session->get_system()->get_my_state() == MULTI_SSS_JOINED);
		_session->post_external_data(true);
		break;

	case MULTI_SOS_FAILED:
		// Will cause a delete to fire after this finishes - which will call the Close Handle part
		if (_session->_desired_state > MULTI_SS_ALLOCATED) {
			_session->_desired_state = MULTI_SS_ALLOCATED;
		}
		break;

	default:
		break;
	}
}

#else

	multi_session_op_status multi_session_op_create::start()
	{
		return MULTI_SOS_FAILED;
	}

	multi_session_op_status multi_session_op_create::process()
	{
		return MULTI_SOS_FAILED;
	}

	void multi_session_op_create::exit( multi_session_op_status status )
	{
	}

#endif // defined (__PS3)

multi_session_op_status multi_session_op_create::process_for_zombie()
{
	_session->_info.platform.invalidate();
	return post_create();
}


void multi_session_op_create::cancel()
{
	#if defined(__LIVE_ENABLED)
		XCancelOverlapped( &_overlapped );
		memset( &_overlapped, 0, sizeof(XOVERLAPPED) );

		if (_session->_info.platform.handle != INVALID_HANDLE_VALUE) {
			// Didn't get far enough to finish a close, so bugger it and continue on
			CloseHandle( _session->_info.platform.handle );
			_session->_info.platform.handle = INVALID_HANDLE_VALUE;
		}
	#elif defined(__NX__)
		nex_state = NEX_CREATE_STATE_NONE;
		if (nex_data != NULL) {
			nex_data->ctx.Cancel();
			nex_data->ctx.Reset();
			nex_data->p_ctx.Cancel();
			nex_data->p_ctx.Reset();
		}
	#endif
}

// Called after the session has properly been created
// session state should now be ALIVE, but we still have a possibility to fail
//
multi_session_op_status multi_session_op_create::post_create()
{
	// Alright, tell the system that I have successfully moved states
	_session->_state = MULTI_SS_ALIVE;
	multi_session_op_status	sos = MULTI_SOS_SUCCESS;
	_session->trigger_event( MSE_SESSION_CREATED, &sos );

	// Copy the options from our operation back to the session.  start() may have modified them and
	// the get connection address routines below need the session to be up-to-date for PS3.
	_session->_info.options.copy(options);

	connection_address host_address, my_address;
	if ( (_session->get_my_connection_address(my_address) == MULTI_SOS_FAILED) 
		|| (_session->get_host_connection_address(host_address) == MULTI_SOS_FAILED) ) {

		V_ERROR_MSG( "Was unable to get all the addresses I needed" );
		return MULTI_SOS_FAILED;
	}

#if defined(USE_IPV6)
	char strAddr[INET6_ADDRSTRLEN] = {0};
#if !defined (__RELEASE_FINAL)	// HVS_JRS 5/5/2014 avoid debug in final
	dispIP6((unsigned short *)&my_address.addr.address6.sin6_addr, strAddr, INET6_ADDRSTRLEN);
#endif
	debug_spew("multi_session_op_create::post_create() my_address addr: %s : %d",
		strAddr, ntohs(my_address.addr.port));
	char strAddr2[INET6_ADDRSTRLEN] = {0};
#if !defined (__RELEASE_FINAL)	// HVS_JRS 5/5/2014 avoid debug in final
	dispIP6((unsigned short *)&host_address.addr.address6.sin6_addr, strAddr2, INET6_ADDRSTRLEN);
#endif
	debug_spew("multi_session_op_create::post_create() host_address addr: %s : %d",
		strAddr2, ntohs(host_address.addr.port));
#endif

#if defined(__PS3)
	// Apparently, there are cases where the PS3 can't use addresses for this check. Not really sure why...
	bool is_host = My_player_uid.is_equal( _session->get_host_info()->uid );
#elif defined(__PS4)	// HVS_JRS 2/4/2014 enable ps4 networking
	bool is_host = My_player_uid.is_equal( _session->get_host_info()->uid );
#elif defined(__NX__)
	// the address is not set yet, better use network service account ids
	bool is_host = My_player_uid.is_equal(_session->get_host_info()->uid);
#elif defined(__PC_GP)
	bool is_host = my_address.is_equal(host_address);
#else // __PS3
	// For LIVE we can't use uid as it changes if you switch from a local profile to a live one.
	bool is_host = my_address.is_equal(host_address);
#endif // _PS3

	V_ASSERT( _session->_zombified || is_host );
	// HVS_JRS 4/21/2014 check on this v_assert firing second game in
	debug_spew("multi_session_op_create::post_create() _session->_zombified:%d is_host:%d", _session->_zombified, is_host);

	if (is_host == true) {
//#ifdef __XBOX3	// this will be set sooner in multi_session_op_create::start()
//		extern bool bIsHost;
//		bIsHost = true;
//#endif
		_session->_info.host.uid.set( My_player_uid );
		_session->_info.host.addr.copy( my_address );
		_session->_info.host.index = desired_index;
		_session->_host_connection = _session->_me_connection;
	}

	V_ASSERT( _session->_host_connection != NULL );

	multi_connection_reset( _session->_me_connection, _session->_me_connection->index_in_session, &my_address, &My_player_uid );
	if (_session->_zombified) {
		V_ASSERT( _session->_me_connection->is_connected() == false );
	}

	CEG_SELF_CHECK();

// HVS_JRS 2/4/2014 #if !defined(__PS4) || defined(HVS_NEED_PS4_VERSION)
	if (_session->in_dead_state() == false) {
		_session->_me_connection->connect();
		_session->add_connection_user(_session->_me_connection);
	}
// HVS_JRS 2/4/2014 #endif

	multi_synced_system *system = _session->_system;
	if (system == NULL) {
		_session->_register_system( options.system_id );
		system = _session->_system;
		V_ASSERT_RETURN_VALUE( system != NULL, MULTI_SOS_FAILED );

		// Register the callbacks for it
		// I need to set joined before I join 
		// since I call the normal callback to late to be taken into
		// account for host reassignment
		_session->_me_connection->joined = true;
		system->join( MULTI_SSS_JOINED );

	} else {
		// Should never hit this, all the callbacks for zombie should exit and recreate the 
		// same frame, and it will set me back to host since everyone should still THINK I'm host
		// Though I do unregister the system locally
		V_ASSERT(_session->_reviving || _session->_zombifying || _session->_zombified); 
		if (system->am_i_joined() == false) {
			system->join( MULTI_SSS_JOINED );
		} else {
			system->set_my_state( MULTI_SSS_JOINED );

			// Need to redo the joined callback, since I lost it due to the connection reset
			_session->connection_joined( _session->get_me_connection() );
		}
	}

	V_ASSERT( system->get_owner() == _session->_host_connection );
	return MULTI_SOS_SUCCESS;
}
